/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_AI_WEBSEARCHCLIENT_H
#define OKULAR_AI_WEBSEARCHCLIENT_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkReply;

struct WebSearchResult {
    QString title;
    QString url;
    QString snippet;
};

/**
 * Lightweight web search without an API key.
 *
 * Brave Search is used first; if it is unavailable (network error or a page
 * without results) the client retries DuckDuckGo's HTML endpoint. Only the
 * top few organic results are returned.
 */
class WebSearchClient : public QObject
{
    Q_OBJECT

public:
    explicit WebSearchClient(QObject *parent = nullptr);
    ~WebSearchClient() override;

    bool isBusy() const;
    void search(const QString &query);
    void abort();

Q_SIGNALS:
    void finished(const QVector<WebSearchResult> &results);
    void errorOccurred(const QString &message);

private:
    enum Backend {
        Brave,
        DuckDuckGo,
    };

    void startRequest(Backend backend);
    void onReplyFinished();
    void parseBrave(const QString &html, QVector<WebSearchResult> *results) const;
    void parseDuckDuckGo(const QString &html, QVector<WebSearchResult> *results) const;

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QString m_query;
    Backend m_backend = Brave;
    bool m_retried = false;
};

#endif
