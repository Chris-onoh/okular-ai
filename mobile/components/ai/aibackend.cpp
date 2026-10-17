/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aibackend.h"

#include "../documentitem.h"
#include "llmclient.h"
#include "modelcatalog.h"
#include "websearchclient.h"

#include <core/document.h>
#include <core/page.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace
{
constexpr int kChunkSize = 1400;
constexpr int kChunkOverlap = 150;
constexpr qint64 kMaxExtractedCharacters = 2 * 1024 * 1024;
constexpr int kMobileMaxContext = 32000;
constexpr int kHistoryLimit = 20;
}

AiBackend::AiBackend(QObject *parent)
    : QObject(parent)
    , m_config(AiConfig::load())
    , m_chatModel(new AiChatModel(this))
    , m_client(new LlmClient(this))
    , m_catalog(new ModelCatalog(this))
    , m_webSearch(new WebSearchClient(this))
{
    connect(m_client, &LlmClient::started, this, [this]() {
        setStatus(tr("Waiting for the model…"));
    });

    connect(m_client, &LlmClient::chunkReceived, this, [this](const QString &chunk) {
        if (m_assistantRow >= 0) {
            m_chatModel->appendText(m_assistantRow, chunk);
        }
    });

    connect(m_client, &LlmClient::reasoningChunk, this, [this](const QString &chunk) {
        if (m_assistantRow >= 0 && m_config.showThinking && m_thinkingSupport != 0) {
            m_chatModel->appendReasoning(m_assistantRow, chunk);
        }
    });

    connect(m_client, &LlmClient::finished, this, [this](const QString &fullText) {
        if (!fullText.isEmpty()) {
            QJsonObject assistantMessage;
            assistantMessage.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            assistantMessage.insert(QStringLiteral("content"), fullText);
            m_history.append(assistantMessage);
            if (m_pendingAssistantSerial >= 0) {
                m_historySerials.append(m_pendingAssistantSerial);
            }
            trimHistory();
        }

        if (m_assistantRow >= 0) {
            m_chatModel->finishMessage(m_assistantRow);
            m_assistantRow = -1;
        }
        m_pendingAssistantSerial = -1;
        if (m_stoppedByUser) {
            m_chatModel->addMessage(QStringLiteral("system"), tr("Generation stopped."), -1);
        }
        m_stoppedByUser = false;
        setBusy(false);
        setStatus(tr("Ready"));
    });

    connect(m_client, &LlmClient::errorOccurred, this, [this](const QString &message) {
        if (m_assistantRow >= 0) {
            m_chatModel->finishMessage(m_assistantRow);
            m_assistantRow = -1;
        }
        m_chatModel->addMessage(QStringLiteral("system"), tr("Error: %1").arg(message), -1);
        m_pendingAssistantSerial = -1;
        m_stoppedByUser = false;
        setBusy(false);
        setStatus(tr("Ready"));
    });

    connect(m_webSearch, &WebSearchClient::finished, this, &AiBackend::onWebSearchFinished);
    connect(m_webSearch, &WebSearchClient::errorOccurred, this, &AiBackend::onWebSearchFailed);

    connect(m_catalog, &ModelCatalog::modelsFound, this, [this](int source, const QStringList &models) {
        Q_UNUSED(source)
        m_availableModels = models;
        Q_EMIT availableModelsChanged();
        if (models.isEmpty()) {
            setStatus(tr("No models were found."));
            return;
        }
        if (m_config.model.isEmpty() || !models.contains(m_config.model)) {
            m_config.model = models.first();
            m_config.save();
            Q_EMIT modelChanged();
            refreshThinkingCapability();
        }
        setStatus(tr("%1 model(s) found.").arg(models.size()));
    });

    connect(m_catalog, &ModelCatalog::probeFailed, this, [this](int source, const QString &message) {
        Q_UNUSED(source)
        setStatus(message);
    });

    connect(m_catalog, &ModelCatalog::thinkingSupportFound, this, [this](int, const QString &model, bool supported) {
        if (model != m_config.model) {
            return;
        }
        m_thinkingSupport = supported ? 1 : 0;
        Q_EMIT thinkingCapabilityChanged();
    });

    connect(m_catalog, &ModelCatalog::thinkingSupportUnknown, this, [this](int, const QString &model) {
        if (model != m_config.model) {
            return;
        }
        m_thinkingSupport = -1;
        Q_EMIT thinkingCapabilityChanged();
    });

    updateSetupState();
    setStatus(m_needsSetup ? tr("No API key configured. Open AI settings to choose a provider.") : tr("Ready"));
    refreshThinkingCapability();
}

AiBackend::~AiBackend() = default;

AiChatModel *AiBackend::chatModel() const
{
    return m_chatModel;
}

QObject *AiBackend::document() const
{
    return m_document;
}

void AiBackend::setDocument(QObject *document)
{
    auto *item = qobject_cast<DocumentItem *>(document);
    if (item == m_document) {
        return;
    }

    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = item;
    if (m_document) {
        connect(m_document, &DocumentItem::currentPageChanged, this, &AiBackend::updateDocumentInfo);
        connect(m_document, &DocumentItem::pageCountChanged, this, &AiBackend::updateDocumentInfo);
        connect(m_document, &DocumentItem::openedChanged, this, [this]() {
            invalidateDocumentCache();
            updateDocumentInfo();
        });
        connect(m_document, &DocumentItem::urlChanged, this, [this]() {
            invalidateDocumentCache();
            updateDocumentInfo();
        });
    }

    invalidateDocumentCache();
    Q_EMIT documentChanged();
    updateDocumentInfo();
}

bool AiBackend::busy() const
{
    return m_busy;
}

QString AiBackend::status() const
{
    return m_status;
}

bool AiBackend::thinkingEnabled() const
{
    return m_config.showThinking;
}

void AiBackend::setThinkingEnabled(bool enabled)
{
    if (m_config.showThinking == enabled) {
        return;
    }
    m_config.showThinking = enabled;
    m_config.save();
    Q_EMIT thinkingEnabledChanged();
}

bool AiBackend::thinkingSupported() const
{
    return m_thinkingSupport != 0;
}

bool AiBackend::thinkingSupportKnown() const
{
    return m_thinkingSupport >= 0;
}

bool AiBackend::webSearchEnabled() const
{
    return m_config.webSearchEnabled;
}

void AiBackend::setWebSearchEnabled(bool enabled)
{
    if (m_config.webSearchEnabled == enabled) {
        return;
    }
    m_config.webSearchEnabled = enabled;
    m_config.save();
    Q_EMIT webSearchEnabledChanged();
}

bool AiBackend::pageScope() const
{
    return m_pageScope;
}

void AiBackend::setPageScope(bool pageScope)
{
    if (m_pageScope == pageScope) {
        return;
    }
    m_pageScope = pageScope;
    Q_EMIT pageScopeChanged();
}

bool AiBackend::needsSetup() const
{
    return m_needsSetup;
}

bool AiBackend::keyConfigured() const
{
    return m_keyConfigured;
}

QString AiBackend::provider() const
{
    return m_config.provider;
}

void AiBackend::setProvider(const QString &provider)
{
    if (provider == m_config.provider) {
        return;
    }

    m_config.provider = provider;
    if (provider == QLatin1String("deepseek")) {
        m_config.endpoint = QStringLiteral("https://api.deepseek.com/chat/completions");
        if (m_config.model.isEmpty()) {
            m_config.model = QStringLiteral("deepseek-flash");
        }
    } else if (provider == QLatin1String("ollama")) {
        m_config.endpoint = ModelCatalog::defaultOllamaChatEndpoint();
        m_config.model.clear();
    } else if (provider == QLatin1String("llamacpp")) {
        m_config.endpoint = ModelCatalog::defaultLlamaCppChatEndpoint();
        m_config.model.clear();
    }

    m_config.save();
    m_availableModels.clear();
    Q_EMIT providerChanged();
    Q_EMIT endpointChanged();
    Q_EMIT modelChanged();
    Q_EMIT availableModelsChanged();
    updateSetupState();
    refreshThinkingCapability();
    detectModels();
}

void AiBackend::applyProvider(const QString &provider)
{
    setProvider(provider);
}

QString AiBackend::endpoint() const
{
    return m_config.endpoint;
}

void AiBackend::setEndpoint(const QString &endpoint)
{
    const QString trimmed = endpoint.trimmed();
    if (trimmed.isEmpty() || trimmed == m_config.endpoint) {
        return;
    }
    m_config.endpoint = trimmed;
    m_config.save();
    m_availableModels.clear();
    Q_EMIT endpointChanged();
    Q_EMIT availableModelsChanged();
    updateSetupState();
    refreshThinkingCapability();
}

QString AiBackend::model() const
{
    return m_config.model;
}

void AiBackend::setModel(const QString &model)
{
    const QString trimmed = model.trimmed();
    if (trimmed == m_config.model) {
        return;
    }
    m_config.model = trimmed;
    m_config.save();
    Q_EMIT modelChanged();
    refreshThinkingCapability();
}

QStringList AiBackend::availableModels() const
{
    return m_availableModels;
}

QString AiBackend::documentTitle() const
{
    return m_documentTitle;
}

int AiBackend::pageCount() const
{
    return m_documentPageCount;
}

int AiBackend::currentPage() const
{
    return m_document ? m_document->currentPage() + 1 : 0;
}

bool AiBackend::rememberKey() const
{
    return m_config.saveKey;
}

void AiBackend::setRememberKey(bool remember)
{
    if (m_config.saveKey == remember) {
        return;
    }
    m_config.saveKey = remember;
    m_config.save();
    Q_EMIT rememberKeyChanged();
    updateSetupState();
}

int AiBackend::contextBudget() const
{
    return m_config.maxContextChars;
}

void AiBackend::setContextBudget(int budget)
{
    const int bounded = qBound(4000, budget, 64000);
    if (bounded == m_config.maxContextChars) {
        return;
    }
    m_config.maxContextChars = bounded;
    m_config.save();
    Q_EMIT contextBudgetChanged();
}

void AiBackend::send(const QString &text)
{
    const QString question = text.trimmed();
    if (question.isEmpty() || m_busy) {
        return;
    }
    if (!m_document || !m_document->isOpened() || m_document->pageCount() <= 0) {
        m_chatModel->addMessage(QStringLiteral("system"), tr("Open a document first."), -1);
        return;
    }

    QString error;
    if (!ensureDocumentText(&error)) {
        m_chatModel->addMessage(QStringLiteral("system"), error, -1);
        return;
    }

    QString keyError;
    QString apiKey = m_config.resolveApiKey(nullptr, &keyError);
    if (apiKey.isEmpty() && AiConfig::isLocalEndpoint(m_config.endpoint)) {
        apiKey = QStringLiteral("local");
    }
    if (apiKey.isEmpty()) {
        updateSetupState();
        m_chatModel->addMessage(QStringLiteral("system"),
                                tr("No API key available. Open AI settings to add one, or use a local model.\n%1").arg(keyError),
                                -1);
        return;
    }

    const int page = currentPage();
    if (m_config.webSearchEnabled) {
        m_pendingQuestion = question;
        m_pendingPage = page;
        m_webContext.clear();
        setBusy(true);
        setStatus(tr("Searching the web…"));
        m_webSearch->search(question);
        return;
    }

    proceed(question, page);
}

void AiBackend::stop()
{
    if (m_webSearch->isBusy()) {
        m_webSearch->abort();
        m_pendingQuestion.clear();
        setBusy(false);
        setStatus(tr("Ready"));
        return;
    }
    if (!m_client->isBusy()) {
        return;
    }
    m_stoppedByUser = true;
    m_client->abort();
}

void AiBackend::clearConversation()
{
    m_history = QJsonArray();
    m_historySerials.clear();
    m_webContext.clear();
    m_pendingQuestion.clear();
    m_assistantRow = -1;
    m_chatModel->clearMessages();
    setStatus(tr("Ready"));
}

void AiBackend::goToPage(int page)
{
    if (!m_document || page <= 0) {
        return;
    }
    m_document->setCurrentPage(qBound(0, page - 1, qMax(0, m_document->pageCount() - 1)));
}

void AiBackend::detectModels()
{
    const QString providerName = m_config.provider;
    if (providerName == QLatin1String("deepseek")) {
        m_availableModels = {QStringLiteral("deepseek-flash"), QStringLiteral("deepseek-v4-pro")};
        Q_EMIT availableModelsChanged();
        if (m_config.model.isEmpty()) {
            setModel(m_availableModels.first());
        }
        return;
    }

    if (!AiConfig::isLocalEndpoint(m_config.endpoint)) {
        setStatus(tr("Model discovery is only available for local providers."));
        return;
    }

    const QString base = ModelCatalog::baseFromEndpoint(m_config.endpoint);
    m_availableModels.clear();
    Q_EMIT availableModelsChanged();
    if (providerName == QLatin1String("ollama")) {
        setStatus(tr("Looking for Ollama…"));
        m_catalog->probeOllama(base.isEmpty() ? QStringLiteral("http://127.0.0.1:11434") : base);
    } else {
        setStatus(tr("Looking for a local llama.cpp server…"));
        m_catalog->probeLlamaCpp(base.isEmpty() ? QStringLiteral("http://127.0.0.1:8080") : base);
    }
}

void AiBackend::setApiKey(const QString &key)
{
    m_config.apiKey = key.trimmed();
    if (m_config.saveKey) {
        m_config.save();
    }
    updateSetupState();
}

QString AiBackend::takeMessageForEdit(int row)
{
    if (m_busy) {
        return {};
    }
    const int serial = m_chatModel->messageSerial(row);
    const QString text = m_chatModel->messageText(row);
    if (serial < 0) {
        return {};
    }

    const qsizetype index = m_historySerials.indexOf(serial);
    if (index >= 0) {
        while (m_history.size() > index) {
            m_history.removeAt(m_history.size() - 1);
        }
        while (m_historySerials.size() > index) {
            m_historySerials.removeLast();
        }
    }
    m_chatModel->removeFromSerial(serial);
    return text;
}

void AiBackend::refreshThinkingCapability()
{
    if (m_config.provider == QLatin1String("deepseek")) {
        m_thinkingSupport = 1;
        Q_EMIT thinkingCapabilityChanged();
        return;
    }

    if (!AiConfig::isLocalEndpoint(m_config.endpoint)) {
        m_thinkingSupport = -1;
        Q_EMIT thinkingCapabilityChanged();
        return;
    }

    m_thinkingSupport = -1;
    Q_EMIT thinkingCapabilityChanged();

    const QString base = ModelCatalog::baseFromEndpoint(m_config.endpoint);
    if (m_config.provider == QLatin1String("ollama")) {
        m_catalog->probeThinkingSupport(ModelCatalog::Ollama, m_config.model, base);
    } else if (m_config.provider == QLatin1String("llamacpp") || m_config.provider == QLatin1String("custom")) {
        m_catalog->probeThinkingSupport(ModelCatalog::LlamaCpp, m_config.model, base);
    }
}

void AiBackend::invalidateDocumentCache()
{
    m_documentId.clear();
    m_documentPageCount = 0;
    m_documentTitle.clear();
    m_pages.clear();
}

bool AiBackend::ensureDocumentText(QString *error)
{
    if (!m_document || !m_document->isOpened()) {
        *error = tr("Open a document first.");
        return false;
    }
    Okular::Document *document = m_document->document();
    if (!document) {
        *error = tr("The document is still loading.");
        return false;
    }

    const uint pages = document->pages();
    const QString id = m_document->url().toString();
    if (id == m_documentId && int(pages) == m_documentPageCount && m_pages.size() == int(pages)) {
        return true;
    }

    m_pages.clear();
    m_pages.reserve(int(pages));
    qint64 used = 0;
    for (uint index = 0; index < pages; ++index) {
        const Okular::Page *page = document->page(int(index));
        QString text = page ? page->text() : QString();
        if (used + text.size() > kMaxExtractedCharacters) {
            text.truncate(int(qMax<qint64>(0, kMaxExtractedCharacters - used)));
        }
        used += text.size();
        m_pages.append(text);
        if (index > 0 && (index % 16) == 0) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }

    m_documentId = id;
    m_documentPageCount = int(pages);
    m_documentTitle = m_document->windowTitleForDocument();
    Q_EMIT documentInfoChanged();
    return true;
}

QString AiBackend::buildContext(const QString &question, int pageNumber, QString *notice) const
{
    if (m_pages.isEmpty()) {
        return {};
    }

    const int budget = qMin(m_config.maxContextChars, kMobileMaxContext);
    QStringList lines;
    lines.append(tr("Document: %1").arg(m_documentTitle));
    lines.append(tr("Current page: %1 of %2").arg(pageNumber).arg(m_pages.size()));

    if (m_pageScope) {
        const int pageIndex = qBound(1, pageNumber, m_pages.size()) - 1;
        const QString text = m_pages.at(pageIndex).trimmed();
        if (!text.isEmpty()) {
            lines.append(QStringLiteral("[page %1]\n%2").arg(pageIndex + 1).arg(text.left(budget)));
            if (text.size() > budget) {
                *notice = tr("The current page was truncated to fit the context budget.");
            }
        }
        return lines.join(QLatin1Char('\n'));
    }

    qint64 totalCharacters = 0;
    for (const QString &text : m_pages) {
        totalCharacters += text.size();
    }

    if (totalCharacters <= budget) {
        int used = 0;
        for (int index = 0; index < m_pages.size() && used < budget; ++index) {
            const QString text = m_pages.at(index).trimmed();
            if (text.isEmpty()) {
                continue;
            }
            const QString block = QStringLiteral("[page %1]\n%2").arg(index + 1).arg(text);
            if (used + block.size() > budget) {
                lines.append(block.left(budget - used));
                *notice = tr("The document was truncated to fit the context budget.");
                break;
            }
            lines.append(block);
            used += block.size();
        }
        return lines.join(QLatin1Char('\n'));
    }

    static const QRegularExpression wordPattern(QStringLiteral("[\\p{L}\\p{N}][\\p{L}\\p{N}_-]+"));
    static const QSet<QString> stopWords = {
        QStringLiteral("the"),   QStringLiteral("and"),  QStringLiteral("for"),    QStringLiteral("are"),
        QStringLiteral("but"),   QStringLiteral("not"),  QStringLiteral("you"),    QStringLiteral("this"),
        QStringLiteral("that"),  QStringLiteral("with"), QStringLiteral("what"),   QStringLiteral("which"),
        QStringLiteral("how"),   QStringLiteral("why"),  QStringLiteral("when"),   QStringLiteral("where"),
        QStringLiteral("from"),  QStringLiteral("into"), QStringLiteral("does"),   QStringLiteral("did"),
        QStringLiteral("about"), QStringLiteral("please"), QStringLiteral("explain"), QStringLiteral("summarize"),
        QStringLiteral("document"), QStringLiteral("page"), QStringLiteral("tell"), QStringLiteral("give"),
    };

    QStringList terms;
    QSet<QString> seenTerms;
    auto iterator = wordPattern.globalMatch(question.toLower());
    while (iterator.hasNext()) {
        const QString term = iterator.next().captured();
        if (term.size() >= 3 && !stopWords.contains(term) && !seenTerms.contains(term)) {
            seenTerms.insert(term);
            terms.append(term);
        }
    }

    struct Chunk {
        int page = 0;
        int order = 0;
        QString text;
        int score = 0;
    };

    QVector<Chunk> chunks;
    for (int page = 0; page < m_pages.size(); ++page) {
        const QString text = m_pages.at(page);
        int position = 0;
        int order = 0;
        while (position < text.size()) {
            int end = qMin(text.size(), position + kChunkSize);
            const int newline = text.indexOf(QLatin1Char('\n'), end);
            if (newline > end && newline - end < 300) {
                end = newline;
            }
            const QString chunkText = text.mid(position, end - position).trimmed();
            if (!chunkText.isEmpty()) {
                Chunk chunk;
                chunk.page = page;
                chunk.order = order++;
                chunk.text = chunkText;
                const QString lower = chunkText.toLower();
                for (const QString &term : std::as_const(terms)) {
                    chunk.score += lower.count(term);
                }
                if (page + 1 == pageNumber) {
                    chunk.score += 3;
                }
                chunks.append(chunk);
            }
            if (end >= text.size()) {
                break;
            }
            position = qMax(position + 1, end - kChunkOverlap);
        }
    }

    std::stable_sort(chunks.begin(), chunks.end(), [](const Chunk &left, const Chunk &right) {
        return left.score > right.score;
    });

    QVector<Chunk> selected;
    int used = lines.join(QLatin1Char('\n')).size();
    for (const Chunk &chunk : std::as_const(chunks)) {
        const int cost = chunk.text.size() + 24;
        if (used + cost > budget) {
            continue;
        }
        selected.append(chunk);
        used += cost;
        if (used > budget - 500) {
            break;
        }
    }

    std::stable_sort(selected.begin(), selected.end(), [](const Chunk &left, const Chunk &right) {
        if (left.page != right.page) {
            return left.page < right.page;
        }
        return left.order < right.order;
    });

    for (const Chunk &chunk : std::as_const(selected)) {
        lines.append(QStringLiteral("[page %1]\n%2").arg(chunk.page + 1).arg(chunk.text));
    }
    *notice = tr("The most relevant parts of this large document were used.");
    return lines.join(QLatin1Char('\n'));
}

void AiBackend::proceed(const QString &question, int pageNumber)
{
    QString notice;
    const QString context = buildContext(question, pageNumber, &notice);
    sendQuestion(question, context, notice);
}

void AiBackend::sendQuestion(const QString &question, const QString &context, const QString &notice)
{
    QString error;
    QString apiKey = m_config.resolveApiKey(nullptr, &error);
    if (apiKey.isEmpty() && AiConfig::isLocalEndpoint(m_config.endpoint)) {
        apiKey = QStringLiteral("local");
    }
    if (apiKey.isEmpty()) {
        setBusy(false);
        m_chatModel->addMessage(QStringLiteral("system"), tr("No API key available. %1").arg(error), -1);
        return;
    }

    QString systemContent = tr("You are Okular, a document assistant embedded in the Okular document viewer. "
                               "Answer questions about the document using only the excerpts provided. "
                               "Cite pages as [page N]. If the answer is not in the excerpts, say so clearly "
                               "instead of guessing. Be concise and use short paragraphs or bullet points.");
    if (!m_config.showThinking || m_thinkingSupport == 0) {
        systemContent += tr(" Do not include step-by-step reasoning or hidden analysis; respond directly.");
    }
    if (!context.isEmpty()) {
        systemContent += QStringLiteral("\n\n") + context;
    }
    if (!m_webContext.isEmpty()) {
        systemContent += QStringLiteral("\n\n") + m_webContext;
        systemContent += tr("\n\nSome of the information above comes from the web. Cite the source URL inline.");
        m_webContext.clear();
    }

    QJsonArray messages;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), systemContent);
    messages.append(system);
    for (const QJsonValue value : std::as_const(m_history)) {
        messages.append(value);
    }

    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), question);
    messages.append(userMessage);

    const int userSerial = m_nextSerial++;
    m_history.append(userMessage);
    m_historySerials.append(userSerial);
    trimHistory();

    const int assistantSerial = m_nextSerial++;
    m_pendingAssistantSerial = assistantSerial;
    m_chatModel->addMessage(QStringLiteral("user"), question, userSerial);
    m_assistantRow = m_chatModel->addMessage(QStringLiteral("assistant"), QString(), assistantSerial, true);

    setBusy(true);
    m_client->setEndpoint(m_config.endpoint);
    m_client->setApiKey(apiKey);
    m_client->setModel(m_config.model);
    m_client->setProvider(m_config.provider);
    m_client->setThinkingSupported(m_thinkingSupport != 0);
    m_client->setThinkingEnabled(m_config.showThinking && m_thinkingSupport != 0);
    m_client->sendChat(messages);

    if (!notice.isEmpty()) {
        m_chatModel->addMessage(QStringLiteral("system"), notice, -1);
    }
}

void AiBackend::onWebSearchFinished(const QVector<WebSearchResult> &results)
{
    const QString question = m_pendingQuestion;
    const int page = m_pendingPage;
    m_pendingQuestion.clear();
    if (question.isEmpty()) {
        setBusy(false);
        return;
    }
    m_webContext = formatWebResults(results);
    setStatus(tr("Reading the document…"));
    proceed(question, page);
}

void AiBackend::onWebSearchFailed(const QString &message)
{
    const QString question = m_pendingQuestion;
    const int page = m_pendingPage;
    m_pendingQuestion.clear();
    m_webContext.clear();
    if (question.isEmpty()) {
        setBusy(false);
        return;
    }
    m_chatModel->addMessage(QStringLiteral("system"),
                            tr("Web search failed: %1 Continuing with the document only.").arg(message),
                            -1);
    proceed(question, page);
}

QString AiBackend::formatWebResults(const QVector<WebSearchResult> &results) const
{
    QString text = tr("Web search results (use when relevant and cite the URL):");
    int index = 1;
    for (const WebSearchResult &result : results) {
        text += QStringLiteral("\n%1. %2\n   %3\n   %4").arg(index++).arg(result.title, result.url, result.snippet);
    }
    return text;
}

void AiBackend::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT busyChanged();
}

void AiBackend::setStatus(const QString &status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    Q_EMIT statusChanged();
}

void AiBackend::updateSetupState()
{
    QString error;
    const QString key = m_config.resolveApiKey(nullptr, &error);
    const bool local = AiConfig::isLocalEndpoint(m_config.endpoint);
    const bool needsSetup = key.isEmpty() && !local;
    const bool keyConfigured = !key.isEmpty();

    if (m_needsSetup != needsSetup) {
        m_needsSetup = needsSetup;
        Q_EMIT needsSetupChanged();
    }
    if (m_keyConfigured != keyConfigured) {
        m_keyConfigured = keyConfigured;
        Q_EMIT keyConfiguredChanged();
    }
}

void AiBackend::updateDocumentInfo()
{
    m_documentPageCount = m_document ? m_document->pageCount() : 0;
    m_documentTitle = m_document ? m_document->windowTitleForDocument() : QString();
    Q_EMIT documentInfoChanged();
}

void AiBackend::trimHistory()
{
    while (m_history.size() > kHistoryLimit) {
        m_history.removeAt(0);
        if (!m_historySerials.isEmpty()) {
            m_historySerials.removeFirst();
        }
    }
}
