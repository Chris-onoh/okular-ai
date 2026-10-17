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

    static QString defaultOllamaChatEndpoint();
    static QString defaultOllamaEmbeddingEndpoint();
    static QString defaultLlamaCppChatEndpoint();

Q_SIGNALS:
    void modelsFound(int source, const QStringList &models);
    void probeFailed(int source, const QString &message);

private:
    void probe(int source, const QUrl &url);
    void finish(int source, QNetworkReply *reply);

    QNetworkAccessManager m_network;
    QNetworkReply *m_ollamaReply = nullptr;
    QNetworkReply *m_llamaReply = nullptr;
};

#endif
