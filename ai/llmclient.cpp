/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "llmclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

LlmClient::LlmClient(QObject *parent)
    : QObject(parent)
    , m_endpoint(QStringLiteral("https://api.deepseek.com/chat/completions"))
    , m_model(QStringLiteral("deepseek-flash"))
{
}

LlmClient::~LlmClient()
{
    abort();
}

void LlmClient::setEndpoint(const QString &endpoint)
{
    m_endpoint = endpoint;
}

void LlmClient::setApiKey(const QString &apiKey)
{
    m_apiKey = apiKey;
}

void LlmClient::setModel(const QString &model)
{
    m_model = model;
}

void LlmClient::setProvider(const QString &provider)
{
    m_provider = provider;
}

void LlmClient::setThinkingSupported(bool supported)
{
    m_thinkingSupported = supported;
}

void LlmClient::setThinkingEnabled(bool enabled)
{
    m_thinkingEnabled = enabled;
}

bool LlmClient::isBusy() const
{
    return m_reply != nullptr;
}

void LlmClient::sendChat(const QJsonArray &messages)
{
    if (isBusy()) {
        return;
    }

    if (m_apiKey.trimmed().isEmpty()) {
        Q_EMIT errorOccurred(tr("No API key configured."));
        return;
    }

    QUrl url(m_endpoint.trimmed());
    if (url.scheme().isEmpty()) {
        url = QUrl(QStringLiteral("https://") + m_endpoint.trimmed());
    }
    if (!url.isValid() || url.host().isEmpty()) {
        Q_EMIT errorOccurred(tr("Invalid API endpoint: %1").arg(m_endpoint));
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(180000);

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), true);
    body.insert(QStringLiteral("temperature"), 0.2);

    // Ask the provider not to spend tokens on reasoning. Each provider exposes
    // a different switch; unknown/custom endpoints are left untouched.
    if (m_thinkingSupported && !m_thinkingEnabled) {
        if (m_provider == QLatin1String("deepseek")) {
            QJsonObject thinking;
            thinking.insert(QStringLiteral("type"), QStringLiteral("disabled"));
            body.insert(QStringLiteral("thinking"), thinking);
        } else if (m_provider == QLatin1String("ollama")) {
            body.insert(QStringLiteral("reasoning_effort"), QStringLiteral("none"));
        } else if (m_provider == QLatin1String("llamacpp")) {
            body.insert(QStringLiteral("reasoning_effort"), QStringLiteral("none"));
            QJsonObject templateArguments;
            templateArguments.insert(QStringLiteral("enable_thinking"), false);
            body.insert(QStringLiteral("chat_template_kwargs"), templateArguments);
        }
    }

    m_raw.clear();
    m_buffer.clear();
    m_accumulated.clear();
    m_streamError.clear();
    m_aborted = false;

    m_reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_reply) {
            consume(m_reply->readAll());
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, &LlmClient::onReplyFinished);

    Q_EMIT started();
}

void LlmClient::abort()
{
    if (!m_reply) {
        return;
    }
    m_aborted = true;
    m_reply->abort();
}

void LlmClient::consume(const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }
    m_raw += data;
    m_buffer += data;
    processBuffer();
}

void LlmClient::processBuffer()
{
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty() || line.startsWith(':')) {
            continue;
        }
        if (!line.startsWith("data:")) {
            continue;
        }

        const QByteArray payload = line.mid(5).trimmed();
        if (payload == "[DONE]") {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }

        const QJsonObject object = document.object();
        if (object.contains(QStringLiteral("error"))) {
            m_streamError = errorMessageFromBody(payload);
            if (m_streamError.isEmpty()) {
                m_streamError = tr("The API returned an unknown error.");
            }
            continue;
        }

        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            continue;
        }
        const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
        const QString reasoning = delta.value(QStringLiteral("reasoning_content")).toString();
        if (!reasoning.isEmpty()) {
            Q_EMIT reasoningChunk(reasoning);
        }
        const QString content = delta.value(QStringLiteral("content")).toString();
        if (!content.isEmpty()) {
            m_accumulated += content;
            Q_EMIT chunkReceived(content);
        }
    }
}

void LlmClient::onReplyFinished()
{
    if (!m_reply) {
        return;
    }

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    consume(reply->readAll());

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool wasAborted = m_aborted;
    m_aborted = false;

    if (!m_streamError.isEmpty()) {
        const QString message = m_streamError;
        m_streamError.clear();
        reset();
        reply->deleteLater();
        Q_EMIT errorOccurred(message);
        return;
    }

    if (reply->error() != QNetworkReply::NoError && !wasAborted) {
        QString message = errorMessageFromBody(m_raw);
        if (message.isEmpty()) {
            if (status == 401) {
                message = tr("Authentication failed (HTTP 401). Check your API key.");
            } else if (status == 402) {
                message = tr("Insufficient balance (HTTP 402).");
            } else if (status == 429) {
                message = tr("Rate limit reached (HTTP 429). Please retry shortly.");
            } else if (status > 0) {
                message = tr("Request failed (HTTP %1): %2").arg(status).arg(reply->errorString());
            } else {
                message = reply->errorString();
            }
        }
        reset();
        reply->deleteLater();
        Q_EMIT errorOccurred(message);
        return;
    }

    // Some OpenAI-compatible servers ignore "stream": true and answer with a
    // regular JSON document. Accept that too.
    if (m_accumulated.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(m_raw, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
            if (!choices.isEmpty()) {
                const QJsonObject messageObject = choices.first().toObject().value(QStringLiteral("message")).toObject();
                const QString reasoning = messageObject.value(QStringLiteral("reasoning_content")).toString();
                if (!reasoning.isEmpty()) {
                    Q_EMIT reasoningChunk(reasoning);
                }
                const QString content = messageObject.value(QStringLiteral("content")).toString();
                if (!content.isEmpty()) {
                    m_accumulated = content;
                    Q_EMIT chunkReceived(content);
                }
            }
        }
    }

    const QString result = m_accumulated;
    reset();
    reply->deleteLater();

    if (result.isEmpty() && !wasAborted) {
        Q_EMIT errorOccurred(tr("The server returned an empty response."));
        return;
    }

    Q_EMIT finished(result);
}

void LlmClient::reset()
{
    m_raw.clear();
    m_buffer.clear();
    m_accumulated.clear();
    m_streamError.clear();
}

QString LlmClient::errorMessageFromBody(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return QString();
    }

    const QJsonObject object = document.object();
    const QJsonValue error = object.value(QStringLiteral("error"));
    if (error.isObject()) {
        const QString message = error.toObject().value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            return message;
        }
    } else if (error.isString()) {
        return error.toString();
    }

    const QString message = object.value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
        return message;
    }
    return object.value(QStringLiteral("detail")).toString();
}
