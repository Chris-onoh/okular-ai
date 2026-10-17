/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "websearchclient.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QUrlQuery>

namespace
{
constexpr int kMaxResults = 6;

QString plainText(const QString &html)
{
    return QTextDocumentFragment::fromHtml(html).toPlainText().simplified();
}

QString decodeResultUrl(const QString &href)
{
    QString target = href;
    if (target.startsWith(QLatin1String("//"))) {
        target = QStringLiteral("https:") + target;
    }

    const QUrl url(target);
    const QUrlQuery query(url);
    if (query.hasQueryItem(QStringLiteral("uddg"))) {
        const QString decoded = query.queryItemValue(QStringLiteral("uddg"), QUrl::FullyDecoded);
        if (!decoded.isEmpty()) {
            return decoded;
        }
    }
    return target;
}

QString capture(const QRegularExpression &expression, const QString &text, int group = 1)
{
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(group) : QString();
}

bool isUsableUrl(const QString &url)
{
    return url.startsWith(QLatin1String("http://")) || url.startsWith(QLatin1String("https://"));
}
}

WebSearchClient::WebSearchClient(QObject *parent)
    : QObject(parent)
{
}

WebSearchClient::~WebSearchClient()
{
    abort();
}

bool WebSearchClient::isBusy() const
{
    return m_reply != nullptr;
}

void WebSearchClient::search(const QString &query)
{
    if (isBusy() || query.trimmed().isEmpty()) {
        return;
    }

    m_query = query.trimmed();
    m_retried = false;
    startRequest(Brave);
}

void WebSearchClient::startRequest(Backend backend)
{
    m_backend = backend;

    QUrl url;
    if (backend == Brave) {
        url = QUrl(QStringLiteral("https://search.brave.com/search"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("q"), m_query);
        query.addQueryItem(QStringLiteral("source"), QStringLiteral("web"));
        url.setQuery(query);
    } else {
        url = QUrl(QStringLiteral("https://html.duckduckgo.com/html/"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("q"), m_query);
        url.setQuery(query);
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"));
    request.setRawHeader("Accept", "text/html,application/xhtml+xml");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);

    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, &WebSearchClient::onReplyFinished);
}

void WebSearchClient::abort()
{
    if (m_reply) {
        m_reply->abort();
    }
}

void WebSearchClient::onReplyFinished()
{
    if (!m_reply) {
        return;
    }

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    const QByteArray data = reply->readAll();
    const auto networkError = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkErrorString = reply->errorString();
    reply->deleteLater();

    if (networkError == QNetworkReply::OperationCanceledError) {
        return;
    }

    QVector<WebSearchResult> results;
    if (networkError == QNetworkReply::NoError && status >= 200 && status < 300) {
        const QString html = QString::fromUtf8(data);
        if (m_backend == Brave) {
            parseBrave(html, &results);
        } else {
            parseDuckDuckGo(html, &results);
        }
    }

    if (results.isEmpty() && !m_retried) {
        // The first backend was blocked or had no organic results. Retry once
        // with the other one before giving up.
        m_retried = true;
        startRequest(m_backend == Brave ? DuckDuckGo : Brave);
        return;
    }

    if (results.isEmpty()) {
        if (networkError != QNetworkReply::NoError && status <= 0) {
            Q_EMIT errorOccurred(tr("Web search failed: %1").arg(networkErrorString));
        } else if (status >= 400) {
            Q_EMIT errorOccurred(tr("Web search failed (HTTP %1).").arg(status));
        } else {
            Q_EMIT errorOccurred(tr("No web results were found."));
        }
        return;
    }

    Q_EMIT finished(results);
}

void WebSearchClient::parseBrave(const QString &html, QVector<WebSearchResult> *results) const
{
    static const QRegularExpression blockStart(QStringLiteral(R"rx(<div class="snippet[^"]*"[^>]*data-type="web")rx"));
    static const QRegularExpression hrefPattern(QStringLiteral(R"rx(<a href="([^"]+)")rx"));
    static const QRegularExpression titlePattern(QStringLiteral(R"rx(<div class="title[^"]*"[^>]*>(.*?)</div>)rx"),
                                                 QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression snippetPattern(QStringLiteral(R"rx(<div class="content[^"]*"[^>]*>(.*?)</div>)rx"),
                                                   QRegularExpression::DotMatchesEverythingOption);

    QVector<int> starts;
    auto iterator = blockStart.globalMatch(html);
    while (iterator.hasNext()) {
        starts.append(iterator.next().capturedStart());
    }

    QSet<QString> seen;
    for (int index = 0; index < starts.size() && results->size() < kMaxResults; ++index) {
        const int end = index + 1 < starts.size() ? starts.at(index + 1) : qMin(html.size(), starts.at(index) + 12000);
        const QString block = html.mid(starts.at(index), end - starts.at(index));

        const QString url = decodeResultUrl(capture(hrefPattern, block));
        const QString title = plainText(capture(titlePattern, block));
        if (url.isEmpty() || title.isEmpty() || !isUsableUrl(url) || seen.contains(url)) {
            continue;
        }
        if (url.contains(QLatin1String("search.brave.com"))) {
            continue;
        }

        WebSearchResult result;
        result.title = title;
        result.url = url;
        result.snippet = plainText(capture(snippetPattern, block));
        seen.insert(url);
        results->append(result);
    }
}

void WebSearchClient::parseDuckDuckGo(const QString &html, QVector<WebSearchResult> *results) const
{
    static const QRegularExpression linkBlock(QStringLiteral(R"rx(<a[^>]*class="result__a"[^>]*>.*?</a>)rx"),
                                              QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression snippetBlock(QStringLiteral(R"rx(<a[^>]*class="result__snippet"[^>]*>.*?</a>)rx"),
                                                 QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hrefPattern(QStringLiteral(R"rx(href="([^"]+)")rx"), QRegularExpression::CaseInsensitiveOption);

    QStringList titles;
    QStringList urls;
    auto linkIterator = linkBlock.globalMatch(html);
    while (linkIterator.hasNext() && urls.size() < kMaxResults) {
        const QString block = linkIterator.next().captured();
        const QString href = capture(hrefPattern, block);
        const QString title = plainText(block);
        if (href.isEmpty() || title.isEmpty()) {
            continue;
        }
        titles.append(title);
        urls.append(decodeResultUrl(href));
    }

    QStringList snippets;
    auto snippetIterator = snippetBlock.globalMatch(html);
    while (snippetIterator.hasNext() && snippets.size() < urls.size()) {
        snippets.append(plainText(snippetIterator.next().captured()));
    }

    QSet<QString> seen;
    for (int index = 0; index < urls.size() && results->size() < kMaxResults; ++index) {
        const QString url = urls.at(index);
        if (!isUsableUrl(url) || seen.contains(url)) {
            continue;
        }
        WebSearchResult result;
        result.title = titles.at(index);
        result.url = url;
        result.snippet = index < snippets.size() ? snippets.at(index) : QString();
        seen.insert(url);
        results->append(result);
    }
}
