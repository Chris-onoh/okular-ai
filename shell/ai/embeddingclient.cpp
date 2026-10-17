/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "embeddingclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <utility>

EmbeddingClient::EmbeddingClient(QObject *parent)
    : QObject(parent)
    , m_endpoint(QStringLiteral("http://127.0.0.1:11434/api/embed"))
{
}

EmbeddingClient::~EmbeddingClient()
{
    abort();
}

void EmbeddingClient::setBackend(const QString &backend)
{
    m_backend = backend;
}

void EmbeddingClient::setEndpoint(const QString &endpoint)
{
    m_endpoint = endpoint;
}

void EmbeddingClient::setModel(const QString &model)
{
    m_model = model;
}

void EmbeddingClient::setApiKey(const QString &apiKey)
{
    m_apiKey = apiKey;
}

bool EmbeddingClient::isBusy() const
{
    return m_reply != nullptr;
}

void EmbeddingClient::embed(const QStringList &texts)
{
    if (isBusy() || texts.isEmpty()) {
        return;
    }
    if (m_model.trimmed().isEmpty()) {
        Q_EMIT errorOccurred(tr("No embedding model is configured."));
        return;
    }

    QUrl url(m_endpoint.trimmed());
    if (url.scheme().isEmpty()) {
        url = QUrl(QStringLiteral("http://") + m_endpoint.trimmed());
    }
    if (url.path().isEmpty() || url.path() == QLatin1String("/")) {
        url.setPath(m_backend == QLatin1String("openai") ? QStringLiteral("/v1/embeddings") : QStringLiteral("/api/embed"));
    }
    if (!url.isValid() || url.host().isEmpty()) {
        Q_EMIT errorOccurred(tr("Invalid embedding endpoint: %1").arg(m_endpoint));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_model);
    body.insert(QStringLiteral("input"), QJsonArray::fromStringList(texts));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    if (!m_apiKey.trimmed().isEmpty()) {
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    }
    request.setTransferTimeout(180000);

    m_reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &EmbeddingClient::onReplyFinished);
}

void EmbeddingClient::abort()
{
    if (!m_reply) {
        return;
    }
    m_reply->abort();
}

void EmbeddingClient::onReplyFinished()
{
    if (!m_reply) {
        return;
    }

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (networkError == QNetworkReply::OperationCanceledError) {
        reply->deleteLater();
        return;
    }

    if (networkError != QNetworkReply::NoError) {
        QString message = errorMessageFromBody(data);
        if (message.isEmpty()) {
            message = reply->errorString();
        }
        if (status == 401 || status == 403) {
            message = tr("The embedding server rejected the credentials (HTTP %1).").arg(status);
        } else if (status == 404) {
            message = tr("Embedding model \"%1\" was not found. Run: ollama pull %1").arg(m_model);
        }
        reply->deleteLater();
        Q_EMIT errorOccurred(message);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        reply->deleteLater();
        Q_EMIT errorOccurred(tr("The embedding server returned an invalid response."));
        return;
    }

    const QJsonObject object = document.object();
    QVector<QVector<float>> result;

    const QJsonValue embeddingsValue = object.value(QStringLiteral("embeddings"));
    if (embeddingsValue.isArray()) {
        const QJsonArray array = embeddingsValue.toArray();
        result.reserve(array.size());
        for (const QJsonValue value : array) {
            QVector<float> vector;
            const QJsonArray numbers = value.toArray();
            vector.reserve(numbers.size());
            for (const QJsonValue number : numbers) {
                vector.append(static_cast<float>(number.toDouble()));
            }
            result.append(vector);
        }
    } else if (object.value(QStringLiteral("data")).isArray()) {
        const QJsonArray array = object.value(QStringLiteral("data")).toArray();
        result.reserve(array.size());
        for (const QJsonValue value : array) {
            const QJsonArray numbers = value.toObject().value(QStringLiteral("embedding")).toArray();
            QVector<float> vector;
            vector.reserve(numbers.size());
            for (const QJsonValue number : numbers) {
                vector.append(static_cast<float>(number.toDouble()));
            }
            result.append(vector);
        }
    } else if (object.value(QStringLiteral("embedding")).isArray()) {
        const QJsonArray numbers = object.value(QStringLiteral("embedding")).toArray();
        QVector<float> vector;
        vector.reserve(numbers.size());
        for (const QJsonValue number : numbers) {
            vector.append(static_cast<float>(number.toDouble()));
        }
        result.append(vector);
    }

    reply->deleteLater();

    if (result.isEmpty()) {
        Q_EMIT errorOccurred(tr("The embedding server returned no vectors."));
        return;
    }

    const int dimension = result.first().size();
    if (dimension == 0) {
        Q_EMIT errorOccurred(tr("The embedding server returned empty vectors."));
        return;
    }
    for (const QVector<float> &vector : std::as_const(result)) {
        if (vector.size() != dimension) {
            Q_EMIT errorOccurred(tr("The embedding server returned vectors with inconsistent sizes."));
            return;
        }
    }

    Q_EMIT finished(result);
}

QString EmbeddingClient::errorMessageFromBody(const QByteArray &body)
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
    return object.value(QStringLiteral("message")).toString();
}
