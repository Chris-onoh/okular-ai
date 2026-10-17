/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "modelcatalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QUrl>

ModelCatalog::ModelCatalog(QObject *parent)
    : QObject(parent)
{
}

ModelCatalog::~ModelCatalog()
{
    if (m_ollamaReply) {
        m_ollamaReply->abort();
    }
    if (m_llamaReply) {
        m_llamaReply->abort();
    }
}

QString ModelCatalog::defaultOllamaChatEndpoint()
{
    return QStringLiteral("http://127.0.0.1:11434/v1/chat/completions");
}

QString ModelCatalog::defaultOllamaEmbeddingEndpoint()
{
    return QStringLiteral("http://127.0.0.1:11434/api/embed");
}

QString ModelCatalog::defaultLlamaCppChatEndpoint()
{
    return QStringLiteral("http://127.0.0.1:8080/v1/chat/completions");
}

void ModelCatalog::probeOllama(const QString &baseUrl)
{
    QUrl url(baseUrl.trimmed());
    if (url.scheme().isEmpty()) {
        url = QUrl(QStringLiteral("http://") + baseUrl.trimmed());
    }
    if (!url.isValid() || url.host().isEmpty()) {
        Q_EMIT probeFailed(Ollama, tr("The Ollama address is not valid."));
        return;
    }
    url.setPath(QStringLiteral("/api/tags"));
    probe(Ollama, url);
}

void ModelCatalog::probeLlamaCpp(const QString &baseUrl)
{
    QUrl url(baseUrl.trimmed());
    if (url.scheme().isEmpty()) {
        url = QUrl(QStringLiteral("http://") + baseUrl.trimmed());
    }
    if (!url.isValid() || url.host().isEmpty()) {
        Q_EMIT probeFailed(LlamaCpp, tr("The local server address is not valid."));
        return;
    }
    url.setPath(QStringLiteral("/v1/models"));
    probe(LlamaCpp, url);
}

QString ModelCatalog::baseFromEndpoint(const QString &endpoint, const QString &fallback)
{
    QUrl url(endpoint.trimmed());
    if (url.scheme().isEmpty()) {
        url = QUrl(QStringLiteral("http://") + endpoint.trimmed());
    }
    if (!url.isValid() || url.host().isEmpty()) {
        return fallback;
    }
    QString base = url.scheme() + QStringLiteral("://") + url.host();
    if (url.port() > 0) {
        base += QLatin1Char(':') + QString::number(url.port());
    }
    return base;
}

void ModelCatalog::probeThinkingSupport(int source, const QString &model, const QString &endpointBase)
{
    if (m_thinkingReply) {
        m_thinkingReply->abort();
        m_thinkingReply->deleteLater();
        m_thinkingReply = nullptr;
    }

    const QString trimmedModel = model.trimmed();
    if (trimmedModel.isEmpty()) {
        Q_EMIT thinkingSupportUnknown(source, trimmedModel);
        return;
    }

    m_thinkingSource = source;
    m_thinkingModel = trimmedModel;

    QNetworkRequest request;
    if (source == Ollama) {
        request.setUrl(QUrl(baseFromEndpoint(endpointBase, QStringLiteral("http://127.0.0.1:11434")) + QStringLiteral("/api/show")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    } else {
        request.setUrl(QUrl(baseFromEndpoint(endpointBase, QStringLiteral("http://127.0.0.1:8080")) + QStringLiteral("/props")));
    }
    request.setTransferTimeout(4000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Okular-AI/1.0"));

    if (source == Ollama) {
        QJsonObject body;
        body.insert(QStringLiteral("model"), trimmedModel);
        m_thinkingReply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    } else {
        m_thinkingReply = m_network.get(request);
    }

    QNetworkReply *reply = m_thinkingReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        finishThinkingSupport(reply);
    });
}

void ModelCatalog::finishThinkingSupport(QNetworkReply *reply)
{
    if (m_thinkingReply != reply) {
        reply->deleteLater();
        return;
    }
    m_thinkingReply = nullptr;

    const int source = m_thinkingSource;
    const QString model = m_thinkingModel;
    m_thinkingSource = -1;
    m_thinkingModel.clear();

    const QByteArray data = reply->readAll();
    const auto networkError = reply->error();
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError) {
        Q_EMIT thinkingSupportUnknown(source, model);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        Q_EMIT thinkingSupportUnknown(source, model);
        return;
    }

    const QJsonObject object = document.object();
    if (source == Ollama) {
        if (!object.contains(QStringLiteral("capabilities"))) {
            Q_EMIT thinkingSupportUnknown(source, model);
            return;
        }
        bool supported = false;
        const QJsonArray capabilities = object.value(QStringLiteral("capabilities")).toArray();
        for (const QJsonValue capability : capabilities) {
            if (capability.toString().compare(QStringLiteral("thinking"), Qt::CaseInsensitive) == 0) {
                supported = true;
                break;
            }
        }
        Q_EMIT thinkingSupportFound(source, model, supported);
        return;
    }

    const QString chatTemplate = object.value(QStringLiteral("chat_template")).toString();
    const QJsonObject caps = object.value(QStringLiteral("chat_template_caps")).toObject();
    if (chatTemplate.isEmpty() && caps.isEmpty()) {
        Q_EMIT thinkingSupportUnknown(source, model);
        return;
    }

    const QString haystack = (chatTemplate + QString::fromUtf8(QJsonDocument(caps).toJson(QJsonDocument::Compact))).toLower();
    const bool supported = haystack.contains(QStringLiteral("enable_thinking")) || haystack.contains(QStringLiteral("reasoning"))
        || haystack.contains(QStringLiteral("thinking"));
    Q_EMIT thinkingSupportFound(source, model, supported);
}

void ModelCatalog::probe(int source, const QUrl &url)
{
    QNetworkReply *&slot = source == Ollama ? m_ollamaReply : m_llamaReply;
    if (slot) {
        slot->abort();
        slot->deleteLater();
        slot = nullptr;
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(4000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Okular-AI/1.0"));

    slot = m_network.get(request);
    QNetworkReply *reply = slot;
    connect(reply, &QNetworkReply::finished, this, [this, source, reply]() {
        finish(source, reply);
    });
}

void ModelCatalog::finish(int source, QNetworkReply *reply)
{
    QNetworkReply *&slot = source == Ollama ? m_ollamaReply : m_llamaReply;
    if (slot != reply) {
        // A newer probe replaced this reply.
        reply->deleteLater();
        return;
    }
    slot = nullptr;

    const QByteArray data = reply->readAll();
    const auto networkError = reply->error();
    reply->deleteLater();

    if (networkError == QNetworkReply::OperationCanceledError) {
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        if (source == Ollama) {
            Q_EMIT probeFailed(Ollama, tr("Ollama was not detected. Start it with \"ollama serve\" and try again."));
        } else {
            Q_EMIT probeFailed(LlamaCpp, tr("No llama.cpp server was detected on the default port."));
        }
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        Q_EMIT probeFailed(source, tr("The server returned an unexpected response."));
        return;
    }

    QStringList models;
    const QJsonObject object = document.object();
    if (source == Ollama) {
        const QJsonArray entries = object.value(QStringLiteral("models")).toArray();
        for (const QJsonValue entry : entries) {
            const QJsonObject model = entry.toObject();
            QString name = model.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                name = model.value(QStringLiteral("model")).toString();
            }
            if (name.isEmpty()) {
                continue;
            }
            // Ollama lists embedding-only models next to chat models. Hide the
            // common embedding families so a non-technical user is not offered
            // a model that cannot hold a conversation.
            const QString lower = name.toLower();
            static const QStringList embeddingHints = {
                QStringLiteral("minilm"),
                QStringLiteral("nomic-embed"),
                QStringLiteral("embeddinggemma"),
                QStringLiteral("granite-embedding"),
                QStringLiteral("bge-"),
                QStringLiteral("e5-"),
                QStringLiteral("jina-embed"),
            };
            bool isEmbeddingModel = false;
            for (const QString &hint : embeddingHints) {
                if (lower.contains(hint)) {
                    isEmbeddingModel = true;
                    break;
                }
            }
            if (!isEmbeddingModel) {
                models.append(name);
            }
        }
    } else {
        const QJsonArray entries = object.value(QStringLiteral("data")).toArray();
        for (const QJsonValue entry : entries) {
            const QString id = entry.toObject().value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) {
                models.append(id);
            }
        }
    }

    models.removeDuplicates();
    models.sort(Qt::CaseInsensitive);
    Q_EMIT modelsFound(source, models);
}
