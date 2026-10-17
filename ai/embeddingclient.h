/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_AI_EMBEDDINGCLIENT_H
#define OKULAR_AI_EMBEDDINGCLIENT_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QVector>

class QNetworkReply;

/**
 * Small client for local (or remote) embedding servers.
 *
 * Supports both:
 *   - Ollama:             POST /api/embed       {"model": "...", "input": [...]}
 *   - OpenAI-compatible:  POST /v1/embeddings   {"model": "...", "input": [...]}
 *
 * No API key is required for a local Ollama instance.
 */
class EmbeddingClient : public QObject
{
    Q_OBJECT

public:
    explicit EmbeddingClient(QObject *parent = nullptr);
    ~EmbeddingClient() override;

    void setBackend(const QString &backend); // "ollama" or "openai"
    void setEndpoint(const QString &endpoint);
    void setModel(const QString &model);
    void setApiKey(const QString &apiKey);

    bool isBusy() const;

    /**
     * Requests embeddings for a batch of texts. The finished() signal is
     * emitted with one vector per input text.
     */
    void embed(const QStringList &texts);
    void abort();

Q_SIGNALS:
    void finished(const QVector<QVector<float>> &embeddings);
    void errorOccurred(const QString &message);

private:
    void onReplyFinished();
    static QString errorMessageFromBody(const QByteArray &body);

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QString m_backend = QStringLiteral("ollama");
    QString m_endpoint;
    QString m_model;
    QString m_apiKey;
};

#endif
