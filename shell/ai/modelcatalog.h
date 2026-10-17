/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_AI_MODELCATALOG_H
#define OKULAR_AI_MODELCATALOG_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkReply;
class QUrl;

/**
 * Discovers locally available chat models.
 *
 * Ollama is queried through its native /api/tags endpoint, llama.cpp (and any
 * other OpenAI-compatible local server) through /v1/models. Both probes are
 * asynchronous and short-lived so a missing server never blocks the UI.
 */
class ModelCatalog : public QObject
{
    Q_OBJECT

public:
    enum Source {
        Ollama = 0,
        LlamaCpp = 1,
    };

    explicit ModelCatalog(QObject *parent = nullptr);
    ~ModelCatalog() override;

    /**
     * Probes Ollama at baseUrl (default http://127.0.0.1:11434).
     */
    void probeOllama(const QString &baseUrl = QStringLiteral("http://127.0.0.1:11434"));

    /**
     * Probes an OpenAI-compatible server at baseUrl (default
     * http://127.0.0.1:8080). The /v1/models path is appended.
     */
    void probeLlamaCpp(const QString &baseUrl = QStringLiteral("http://127.0.0.1:8080"));

    /**
     * Checks whether a specific model can produce reasoning output.
     * Ollama is asked through /api/show, llama.cpp through /props.
     */
    void probeThinkingSupport(int source, const QString &model, const QString &endpointBase = QString());

    static QString defaultOllamaChatEndpoint();
    static QString defaultOllamaEmbeddingEndpoint();
    static QString defaultLlamaCppChatEndpoint();

    /**
     * Extracts scheme://host:port from a chat endpoint.
     */
    static QString baseFromEndpoint(const QString &endpoint, const QString &fallback = QString());

Q_SIGNALS:
    void modelsFound(int source, const QStringList &models);
    void probeFailed(int source, const QString &message);
    void thinkingSupportFound(int source, const QString &model, bool supported);
    void thinkingSupportUnknown(int source, const QString &model);

private:
    void probe(int source, const QUrl &url);
    void finish(int source, QNetworkReply *reply);
    void finishThinkingSupport(QNetworkReply *reply);

    QNetworkAccessManager m_network;
    QNetworkReply *m_ollamaReply = nullptr;
    QNetworkReply *m_llamaReply = nullptr;
    QNetworkReply *m_thinkingReply = nullptr;
    int m_thinkingSource = -1;
    QString m_thinkingModel;
};

#endif
