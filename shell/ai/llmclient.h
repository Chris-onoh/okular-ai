/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_AI_LLMCLIENT_H
#define OKULAR_AI_LLMCLIENT_H

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QNetworkReply;

/**
 * Minimal streaming client for OpenAI-compatible chat completion APIs
 * (DeepSeek, OpenAI, or any compatible local server).
 *
 * The API key is only held in memory for the lifetime of a request and is
 * never logged or written to disk by this class.
 */
class LlmClient : public QObject
{
    Q_OBJECT

public:
    explicit LlmClient(QObject *parent = nullptr);
    ~LlmClient() override;

    void setEndpoint(const QString &endpoint);
    void setApiKey(const QString &apiKey);
    void setModel(const QString &model);

    bool isBusy() const;

    /**
     * Starts a streaming chat completion request. Messages must use the
     * OpenAI format, i.e. a list of objects with "role" and "content".
     */
    void sendChat(const QJsonArray &messages);

    /**
     * Aborts the current request, if any. The partial answer is still
     * reported through finished().
     */
    void abort();

Q_SIGNALS:
    void started();
    void chunkReceived(const QString &chunk);
    void reasoningChunk(const QString &chunk);
    void finished(const QString &fullText);
    void errorOccurred(const QString &message);

private:
    void consume(const QByteArray &data);
    void processBuffer();
    void onReplyFinished();
    void reset();

    static QString errorMessageFromBody(const QByteArray &body);

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QString m_endpoint;
    QString m_apiKey;
    QString m_model;

    QByteArray m_raw;
    QByteArray m_buffer;
    QString m_accumulated;
    QString m_streamError;
    bool m_aborted = false;
};

#endif
