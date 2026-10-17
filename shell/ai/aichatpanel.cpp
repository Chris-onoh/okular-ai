/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aichatpanel.h"
#include "chattranscript.h"
#include "embeddingclient.h"
#include "llmclient.h"
#include "modelcatalog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextCursor>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

#include <KLocalizedString>

namespace
{
constexpr int kChunkSize = 1800;
constexpr int kChunkOverlap = 200;
constexpr int kIndexChunkSize = 1200;
constexpr int kIndexChunkOverlap = 150;
constexpr int kEmbeddingBatchSize = 32;
constexpr int kMaxIndexChunks = 1500;
constexpr qint64 kMaxExtractedCharacters = 8 * 1024 * 1024;

QFont panelFont(const QFont &base, int pointDelta, QFont::Weight weight = QFont::Normal)
{
    QFont font = base;
    font.setPointSize(qMax(7, base.pointSize() + pointDelta));
    font.setWeight(weight);
    return font;
}
}

AiChatPanel::AiChatPanel(QWidget *parent)
    : QWidget(parent)
    , m_config(AiConfig::load())
    , m_client(new LlmClient(this))
    , m_indexEmbedder(new EmbeddingClient(this))
    , m_queryEmbedder(new EmbeddingClient(this))
{
    m_webSearch = new WebSearchClient(this);
    setObjectName(QStringLiteral("okular_ai_panel"));
    setMinimumWidth(420);
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout;
    header->setSpacing(8);

    auto *icon = new QLabel(this);
    QIcon themeIcon = QIcon::fromTheme(QStringLiteral("applications-science"));
    if (themeIcon.isNull()) {
        themeIcon = QIcon::fromTheme(QStringLiteral("dialog-information"));
    }
    icon->setPixmap(themeIcon.pixmap(20, 20));
    header->addWidget(icon);

    auto *title = new QLabel(i18n("AI mode"), this);
    title->setObjectName(QStringLiteral("aiTitle"));
    title->setFont(panelFont(font(), 2, QFont::DemiBold));
    header->addWidget(title);
    header->addStretch(1);

    auto makeHeaderToggle = [this, header](const QString &iconName, const QString &fallbackIcon, const QString &text, const QString &tooltip) {
        auto *button = new QToolButton(this);
        button->setObjectName(QStringLiteral("aiHeaderToggle"));
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setText(text);
        button->setIcon(QIcon::fromTheme(iconName, QIcon::fromTheme(fallbackIcon)));
        button->setIconSize(QSize(16, 16));
        button->setFixedHeight(26);
        button->setToolTip(tooltip);
        button->setAccessibleName(tooltip);
        button->setCursor(Qt::PointingHandCursor);
        header->addWidget(button);
        return button;
    };

    m_thinkingButton = makeHeaderToggle(QStringLiteral("help-hint"), QStringLiteral("dialog-information"), i18n("Thinking"), i18n("Show the model's thinking"));
    m_thinkingButton->setChecked(m_config.showThinking);
    m_webButton = makeHeaderToggle(QStringLiteral("applications-internet"), QStringLiteral("network-workgroup"), i18n("Web"), i18n("Search the web for context"));
    m_webButton->setChecked(m_config.webSearchEnabled);

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setObjectName(QStringLiteral("aiModelCombo"));
    m_modelCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_modelCombo->setToolTip(i18n("Chat model"));
    m_modelCombo->setAccessibleName(i18n("Chat model"));

    auto *settingsButton = new QToolButton(this);
    settingsButton->setAutoRaise(true);
    settingsButton->setIcon(QIcon::fromTheme(QStringLiteral("settings-configure"), QIcon::fromTheme(QStringLiteral("preferences-system"))));
    settingsButton->setIconSize(QSize(18, 18));
    settingsButton->setToolTip(i18n("AI settings"));
    settingsButton->setAccessibleName(i18n("AI settings"));
    header->addWidget(settingsButton);

    auto *clearButton = new QToolButton(this);
    clearButton->setAutoRaise(true);
    clearButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear-history"), QIcon::fromTheme(QStringLiteral("edit-clear"))));
    clearButton->setIconSize(QSize(18, 18));
    clearButton->setToolTip(i18n("Clear conversation"));
    clearButton->setAccessibleName(i18n("Clear conversation"));
    header->addWidget(clearButton);

    layout->addLayout(header);

    m_setupBanner = new QFrame(this);
    m_setupBanner->setObjectName(QStringLiteral("aiSetupBanner"));
    m_setupBanner->setVisible(false);
    auto *bannerLayout = new QHBoxLayout(m_setupBanner);
    bannerLayout->setContentsMargins(12, 8, 8, 8);
    bannerLayout->setSpacing(8);
    auto *bannerText = new QLabel(i18n("No API key needed: run a free model on this computer."), m_setupBanner);
    bannerText->setObjectName(QStringLiteral("setupBannerText"));
    bannerText->setWordWrap(true);
    auto *bannerButton = new QPushButton(i18n("Set up local AI"), m_setupBanner);
    bannerButton->setObjectName(QStringLiteral("setupBannerButton"));
    bannerButton->setCursor(Qt::PointingHandCursor);
    bannerLayout->addWidget(bannerText, 1);
    bannerLayout->addWidget(bannerButton);
    layout->addWidget(m_setupBanner);
    connect(bannerButton, &QPushButton::clicked, this, &AiChatPanel::openLocalSetup);

    auto *documentRow = new QHBoxLayout;
    documentRow->setSpacing(8);

    m_documentLabel = new QLabel(this);
    m_documentLabel->setObjectName(QStringLiteral("aiDocumentLabel"));
    m_documentLabel->setFont(panelFont(font(), 0, QFont::DemiBold));
    m_documentLabel->setWordWrap(false);
    m_documentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_documentLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    documentRow->addWidget(m_documentLabel, 1);
    documentRow->addWidget(m_modelCombo);
    layout->addLayout(documentRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("aiStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setFont(panelFont(font(), -1));
    layout->addWidget(m_statusLabel);

    m_transcript = new ChatTranscript(this);
    layout->addWidget(m_transcript, 1);

    auto *composer = new QFrame(this);
    composer->setObjectName(QStringLiteral("composer"));
    auto *composerLayout = new QVBoxLayout(composer);
    composerLayout->setContentsMargins(8, 5, 8, 5);
    composerLayout->setSpacing(4);

    m_input = new QPlainTextEdit(composer);
    m_input->setObjectName(QStringLiteral("aiInput"));
    m_input->setPlaceholderText(i18n("Ask about this document… (Enter to send)"));
    m_input->setMinimumHeight(38);
    m_input->setMaximumHeight(72);
    m_input->setTabChangesFocus(true);
    m_input->setAccessibleName(i18n("Message"));
    m_input->viewport()->setAutoFillBackground(false);
    m_input->installEventFilter(this);
    composerLayout->addWidget(m_input);

    auto *composerBottom = new QHBoxLayout;
    composerBottom->setSpacing(6);

    auto *scopeGroup = new QButtonGroup(this);
    scopeGroup->setExclusive(true);
    m_scopeDocumentButton = new QPushButton(i18n("Document"), composer);
    m_scopeDocumentButton->setObjectName(QStringLiteral("scopeButton"));
    m_scopeDocumentButton->setCheckable(true);
    m_scopeDocumentButton->setChecked(true);
    m_scopeDocumentButton->setAccessibleName(i18n("Ask about the whole document"));
    m_scopePageButton = new QPushButton(i18n("This page"), composer);
    m_scopePageButton->setObjectName(QStringLiteral("scopeButton"));
    m_scopePageButton->setCheckable(true);
    m_scopePageButton->setAccessibleName(i18n("Ask about the current page"));
    scopeGroup->addButton(m_scopeDocumentButton);
    scopeGroup->addButton(m_scopePageButton);
    composerBottom->addWidget(m_scopeDocumentButton);
    composerBottom->addWidget(m_scopePageButton);
    composerBottom->addStretch(1);

    m_stopButton = new QPushButton(i18n("Stop"), composer);
    m_stopButton->setObjectName(QStringLiteral("stopButton"));
    m_stopButton->setVisible(false);
    m_stopButton->setAccessibleName(i18n("Stop generating"));
    composerBottom->addWidget(m_stopButton);

    m_sendButton = new QPushButton(i18n("Send"), composer);
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setAccessibleName(i18n("Send message"));
    composerBottom->addWidget(m_sendButton);

    composerLayout->addLayout(composerBottom);
    layout->addWidget(composer);

    connect(settingsButton, &QToolButton::clicked, this, &AiChatPanel::openSettings);
    connect(clearButton, &QToolButton::clicked, this, &AiChatPanel::clearConversation);
    connect(m_sendButton, &QPushButton::clicked, this, &AiChatPanel::sendMessage);
    connect(m_stopButton, &QPushButton::clicked, this, &AiChatPanel::stopGeneration);
    connect(m_scopePageButton, &QPushButton::toggled, this, [this](bool checked) { m_pageOnly = checked; });
    connect(m_thinkingButton, &QToolButton::toggled, this, [this](bool checked) {
        m_config.showThinking = checked;
        m_config.save();
        if (!checked) {
            m_transcript->finishAssistantReasoning();
        }
        if (m_statusLabel) {
            updateStatus();
        }
    });
    connect(m_webButton, &QToolButton::toggled, this, [this](bool checked) {
        m_config.webSearchEnabled = checked;
        m_config.save();
        updateStatus();
    });
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const QString model = m_modelCombo->itemData(index).toString();
        if (model.isEmpty() || model == m_config.model) {
            return;
        }
        m_config.model = model;
        m_config.save();
        updateStatus();
    });

    connect(m_transcript, &ChatTranscript::pageLinkActivated, this, &AiChatPanel::goToPage);
    connect(m_transcript, &ChatTranscript::suggestionActivated, this, &AiChatPanel::ask);
    connect(m_transcript, &ChatTranscript::editRequested, this, &AiChatPanel::editMessage);

    auto *focusShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Space), this);
    connect(focusShortcut, &QShortcut::activated, this, [this]() {
        m_input->setFocus(Qt::ShortcutFocusReason);
    });

    connect(m_client, &LlmClient::started, this, [this]() {
        m_statusLabel->setText(i18n("Waiting for %1…", m_config.model));
    });

    connect(m_client, &LlmClient::chunkReceived, this, [this](const QString &chunk) {
        m_transcript->appendAssistantChunk(chunk);
    });

    connect(m_client, &LlmClient::reasoningChunk, this, [this](const QString &chunk) {
        if (m_config.showThinking) {
            m_transcript->appendAssistantReasoning(chunk);
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
        m_pendingAssistantSerial = -1;
        m_transcript->finishAssistantReasoning();
        m_transcript->endAssistantMessage();
        if (m_stoppedByUser) {
            m_transcript->addSystemMessage(i18n("Generation stopped."));
        }
        m_stoppedByUser = false;
        setBusy(false);
    });

    connect(m_client, &LlmClient::errorOccurred, this, [this](const QString &message) {
        m_pendingAssistantSerial = -1;
        m_transcript->finishAssistantReasoning();
        m_transcript->endAssistantMessage();
        m_transcript->addSystemMessage(i18n("Error: %1", message));
        m_stoppedByUser = false;
        setBusy(false);
    });

    connect(m_indexEmbedder, &EmbeddingClient::finished, this, [this](const QVector<QVector<float>> &vectors) {
        if (!m_indexing) {
            return;
        }
        const int expected = qMin(kEmbeddingBatchSize, static_cast<int>(m_index.size()) - m_indexBatchStart);
        if (vectors.size() != expected) {
            finishIndexing(false, i18n("The embedding server returned an unexpected number of vectors."));
            return;
        }
        for (int index = 0; index < vectors.size(); ++index) {
            m_index[m_indexBatchStart + index].embedding = normalized(vectors.at(index));
        }
        m_indexBatchStart += vectors.size();
        requestNextEmbeddingBatch();
    });

    connect(m_indexEmbedder, &EmbeddingClient::errorOccurred, this, [this](const QString &message) {
        if (m_indexing) {
            finishIndexing(false, message);
        }
    });

    connect(m_queryEmbedder, &EmbeddingClient::finished, this, [this](const QVector<QVector<float>> &vectors) {
        if (m_pendingQuestion.isEmpty()) {
            return;
        }
        QString notice;
        QString context;
        if (!vectors.isEmpty()) {
            context = buildContextFromIndex(m_pendingQuestion, vectors.first(), m_pendingPage, &notice);
        }
        if (context.isEmpty()) {
            context = buildContext(m_pendingQuestion, m_pendingPage, &notice);
        }
        const QString question = m_pendingQuestion;
        m_pendingQuestion.clear();
        sendQuestion(question, context, notice);
    });

    connect(m_queryEmbedder, &EmbeddingClient::errorOccurred, this, [this](const QString &message) {
        if (m_pendingQuestion.isEmpty()) {
            return;
        }
        QString notice;
        const QString context = buildContext(m_pendingQuestion, m_pendingPage, &notice);
        const QString question = m_pendingQuestion;
        m_pendingQuestion.clear();
        m_transcript->addSystemMessage(i18n("Local embedding lookup failed (%1). Using keyword search.", message));
        sendQuestion(question, context, notice);
    });

    connect(m_webSearch, &WebSearchClient::finished, this, &AiChatPanel::onWebSearchFinished);
    connect(m_webSearch, &WebSearchClient::errorOccurred, this, &AiChatPanel::onWebSearchFailed);

    updateModelCombo();
    applyTheme();
    updateStatus();
}

AiChatPanel::~AiChatPanel() = default;

void AiChatPanel::setActivePartProvider(std::function<QObject *()> provider)
{
    m_activePartProvider = std::move(provider);
}

QObject *AiChatPanel::activePart() const
{
    return m_activePartProvider ? m_activePartProvider() : nullptr;
}

void AiChatPanel::documentChanged()
{
    if (m_queryEmbedder->isBusy()) {
        m_queryEmbedder->abort();
        m_pendingQuestion.clear();
        setBusy(false);
    }
    if (m_indexing) {
        m_indexEmbedder->abort();
        m_indexing = false;
    }
    invalidateDocumentCache();
    updateStatus();
}

void AiChatPanel::focusComposer()
{
    if (m_input) {
        m_input->setFocus(Qt::OtherFocusReason);
    }
}

void AiChatPanel::askFromOutside(const QString &prompt)
{
    ask(prompt);
}

void AiChatPanel::invalidateDocumentCache()
{
    m_pages.clear();
    m_documentTitle.clear();
    m_cachedDocumentId.clear();
    m_cachedPageCount = 0;
    m_cachedPart = nullptr;
}

QString AiChatPanel::documentIndexKey() const
{
    if (m_cachedDocumentId.isEmpty()) {
        return QString();
    }
    QString key = m_cachedDocumentId;
    key += QLatin1Char('|');
    key += QString::number(m_pages.size());
    if (!m_pages.isEmpty()) {
        key += QLatin1Char('|');
        key += m_pages.first().left(80);
        key += m_pages.last().right(80);
    }
    return key;
}

void AiChatPanel::maybeStartIndexing()
{
    if (m_config.embeddingBackend == QLatin1String("none") || m_config.embeddingModel.trimmed().isEmpty()) {
        return;
    }
    if (m_indexing) {
        return;
    }

    const QString key = documentIndexKey();
    const bool sameDocument = m_indexDocumentId == key && m_indexModel == m_config.embeddingModel;
    if (m_indexReady && sameDocument) {
        return;
    }
    if (!m_indexError.isEmpty() && sameDocument) {
        return;
    }

    startIndexing();
}

void AiChatPanel::startIndexing()
{
    m_index = makeIndexChunks();
    m_indexDocumentId = documentIndexKey();
    m_indexModel = m_config.embeddingModel;
    m_indexError.clear();
    m_indexErrorShown = false;
    m_indexBatchStart = 0;
    m_indexReady = false;
    m_indexing = true;

    if (m_index.isEmpty()) {
        finishIndexing(true, QString());
        return;
    }
    requestNextEmbeddingBatch();
}

void AiChatPanel::requestNextEmbeddingBatch()
{
    if (!m_indexing) {
        return;
    }
    const int chunkCount = static_cast<int>(m_index.size());
    if (m_indexBatchStart >= chunkCount) {
        finishIndexing(true, QString());
        return;
    }

    const int end = qMin(m_indexBatchStart + kEmbeddingBatchSize, chunkCount);
    QStringList texts;
    texts.reserve(end - m_indexBatchStart);
    for (int index = m_indexBatchStart; index < end; ++index) {
        texts.append(m_index.at(index).text);
    }

    m_indexEmbedder->setBackend(m_config.embeddingBackend);
    m_indexEmbedder->setEndpoint(m_config.embeddingEndpoint);
    m_indexEmbedder->setModel(m_config.embeddingModel);
    m_indexEmbedder->setApiKey(QString());
    m_indexEmbedder->embed(texts);
    updateStatus();
}

void AiChatPanel::finishIndexing(bool success, const QString &error)
{
    m_indexing = false;
    m_indexReady = success && !m_index.isEmpty();
    m_indexError = success ? QString() : error;
    if (!success) {
        m_index.clear();
    }
    updateStatus();

    if (!success && !m_indexErrorShown) {
        m_indexErrorShown = true;
        QString hint = error;
        if ((hint.contains(QStringLiteral("not found"), Qt::CaseInsensitive)
             || hint.contains(QStringLiteral("not exist"), Qt::CaseInsensitive))
            && !hint.contains(QStringLiteral("ollama pull"))) {
            hint += QLatin1Char('\n') + i18n("Install a local embedding model, for example: ollama pull %1", m_config.embeddingModel);
        }
        appendSystemMessage(i18n("Local document indexing is unavailable: %1", hint));
    }
}

QVector<AiChatPanel::IndexedChunk> AiChatPanel::makeIndexChunks() const
{
    QVector<IndexedChunk> chunks;

    for (int pageIndex = 0; pageIndex < m_pages.size(); ++pageIndex) {
        const QString &pageText = m_pages.at(pageIndex);
        int position = 0;
        while (position < pageText.size()) {
            int end = qMin(position + kIndexChunkSize, static_cast<int>(pageText.size()));
            if (end < pageText.size()) {
                const int newline = pageText.lastIndexOf(QLatin1Char('\n'), end);
                if (newline > position + kIndexChunkSize / 2) {
                    end = newline;
                }
            }

            const QString text = pageText.mid(position, end - position).trimmed();
            if (text.size() >= 20) {
                IndexedChunk chunk;
                chunk.page = pageIndex + 1;
                chunk.order = static_cast<int>(chunks.size());
                chunk.text = text;
                chunks.append(chunk);
            }

            if (end >= pageText.size()) {
                break;
            }
            position = qMax(end - kIndexChunkOverlap, position + 1);
        }
    }

    if (chunks.size() > kMaxIndexChunks) {
        QVector<IndexedChunk> sampled;
        sampled.reserve(kMaxIndexChunks);
        const double step = static_cast<double>(chunks.size()) / kMaxIndexChunks;
        for (int index = 0; index < kMaxIndexChunks; ++index) {
            const int sourceIndex = qMin(static_cast<int>(index * step), static_cast<int>(chunks.size()) - 1);
            IndexedChunk chunk = chunks.at(sourceIndex);
            chunk.order = index;
            sampled.append(chunk);
        }
        return sampled;
    }

    return chunks;
}

QVector<float> AiChatPanel::normalized(const QVector<float> &vector)
{
    double norm = 0.0;
    for (const float value : vector) {
        norm += static_cast<double>(value) * value;
    }
    norm = std::sqrt(norm);

    QVector<float> result = vector;
    if (norm > 1e-9) {
        for (float &value : result) {
            value = static_cast<float>(value / norm);
        }
    }
    return result;
}

float AiChatPanel::dotProduct(const QVector<float> &left, const QVector<float> &right)
{
    const int size = static_cast<int>(qMin(left.size(), right.size()));
    float sum = 0.0f;
    for (int index = 0; index < size; ++index) {
        sum += left.at(index) * right.at(index);
    }
    return sum;
}

QString AiChatPanel::embeddingStatus() const
{
    if (m_config.embeddingBackend == QLatin1String("none")) {
        return i18n("keyword search");
    }
    if (m_indexing) {
        return i18n("indexing %1/%2", m_indexBatchStart, static_cast<int>(m_index.size()));
    }
    const bool currentDocument = m_indexDocumentId == documentIndexKey();
    if (m_indexReady && currentDocument) {
        return i18n("semantic index");
    }
    if (!m_indexError.isEmpty() && currentDocument) {
        return i18n("keyword search (index failed)");
    }
    return i18n("keyword search");
}

QString AiChatPanel::buildContextFromIndex(const QString &question, const QVector<float> &queryEmbedding, int currentPageNumber, QString *notice) const
{
    if (m_index.isEmpty() || queryEmbedding.isEmpty()) {
        return QString();
    }

    const QVector<float> query = normalized(queryEmbedding);
    const int budget = qMax(4000, m_config.maxContextChars);
    const QString title = m_documentTitle.isEmpty() ? documentName() : m_documentTitle;

    QStringList lines;
    lines.append(i18n("Document: %1", title));
    lines.append(i18n("Pages: %1", static_cast<int>(m_pages.size())));
    lines.append(QString());
    int used = static_cast<int>(lines.join(QLatin1Char('\n')).size());

    struct Ranked {
        int index = 0;
        float score = 0.0f;
    };

    QVector<Ranked> ranked;
    ranked.reserve(m_index.size());
    for (int index = 0; index < m_index.size(); ++index) {
        float score = dotProduct(query, m_index.at(index).embedding);
        if (m_index.at(index).page == currentPageNumber) {
            score += 0.05f;
        }
        ranked.append(Ranked{index, score});
    }

    const bool overviewRequest = question.contains(QStringLiteral("summar"), Qt::CaseInsensitive)
        || question.contains(QStringLiteral("overview"), Qt::CaseInsensitive)
        || question.contains(QStringLiteral("main topics"), Qt::CaseInsensitive);

    QVector<int> selected;
    if (overviewRequest) {
        const int targetCount = qMax(1, (budget - used) / (kIndexChunkSize + 24));
        const int step = qMax(1, static_cast<int>(m_index.size()) / targetCount);
        for (int index = 0; index < m_index.size(); index += step) {
            selected.append(index);
        }
        if (selected.size() > targetCount) {
            selected.resize(targetCount);
        }
    } else {
        std::sort(ranked.begin(), ranked.end(), [](const Ranked &left, const Ranked &right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return left.index < right.index;
        });

        int selectionUsed = used;
        for (const Ranked &entry : std::as_const(ranked)) {
            const int cost = static_cast<int>(m_index.at(entry.index).text.size()) + 24;
            if (selectionUsed + cost > budget) {
                continue;
            }
            selected.append(entry.index);
            selectionUsed += cost;
        }
    }

    if (currentPageNumber > 0) {
        bool hasCurrentPage = false;
        for (const int index : std::as_const(selected)) {
            if (m_index.at(index).page == currentPageNumber) {
                hasCurrentPage = true;
                break;
            }
        }
        if (!hasCurrentPage) {
            for (int index = 0; index < m_index.size(); ++index) {
                if (m_index.at(index).page == currentPageNumber) {
                    selected.append(index);
                    break;
                }
            }
        }
    }

    std::sort(selected.begin(), selected.end());

    for (const int index : std::as_const(selected)) {
        const IndexedChunk &chunk = m_index.at(index);
        const int cost = static_cast<int>(chunk.text.size()) + 24;
        if (used + cost > budget) {
            continue;
        }
        lines.append(i18n("--- Page %1 ---", chunk.page));
        lines.append(chunk.text);
        lines.append(QString());
        used += cost;
    }

    if (notice) {
        *notice = i18n("Excerpts were selected with the local semantic index (%1).", m_config.embeddingModel);
    }

    return lines.join(QLatin1Char('\n'));
}

bool AiChatPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool isEnter = keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
        if (isEnter && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            sendMessage();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AiChatPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        applyTheme();
    }
}

void AiChatPanel::sendMessage()
{
    const QString question = m_input->toPlainText().trimmed();
    if (question.isEmpty() || m_client->isBusy()) {
        return;
    }
    m_input->clear();
    ask(question);
}

void AiChatPanel::stopGeneration()
{
    if (m_webSearch->isBusy()) {
        m_webSearch->abort();
        m_pendingWebQuestion.clear();
        setBusy(false);
        return;
    }
    if (m_queryEmbedder->isBusy()) {
        m_queryEmbedder->abort();
        m_pendingQuestion.clear();
        setBusy(false);
        return;
    }
    if (!m_client->isBusy()) {
        return;
    }
    m_stoppedByUser = true;
    m_client->abort();
}

void AiChatPanel::summarizeDocument()
{
    if (m_client->isBusy()) {
        return;
    }
    ask(i18n("Summarize this document. Cover its main topics, structure and conclusions, and cite page numbers."));
}

void AiChatPanel::explainCurrentPage()
{
    if (m_client->isBusy()) {
        return;
    }
    const int page = currentPageNumber();
    if (page <= 0) {
        appendSystemMessage(i18n("No document is open."));
        return;
    }
    ask(i18n("Explain the content of page %1 in clear, simple terms.", page));
}

void AiChatPanel::ask(const QString &question)
{
    if (m_client->isBusy() || m_queryEmbedder->isBusy() || m_webSearch->isBusy()) {
        return;
    }

    if (!activePart()) {
        appendSystemMessage(i18n("Open a document first."));
        return;
    }

    QString error;
    if (!ensureDocumentText(&error)) {
        appendSystemMessage(error);
        return;
    }

    QString keySource;
    QString apiKey = m_config.resolveApiKey(&keySource, &error);
    if (apiKey.isEmpty() && AiConfig::isLocalEndpoint(m_config.endpoint)) {
        apiKey = QStringLiteral("local");
    }
    if (apiKey.isEmpty()) {
        appendSystemMessage(i18n("No API key available. %1 Open AI settings to configure one, or use a free local model.", error));
        return;
    }

    const int page = currentPageNumber();

    if (m_config.webSearchEnabled) {
        m_pendingWebQuestion = question;
        m_pendingWebPage = page;
        m_webContext.clear();
        setBusy(true);
        m_statusLabel->setText(i18n("Searching the web…"));
        m_webSearch->search(question);
        return;
    }

    continueAsk(question, page);
}

void AiChatPanel::continueAsk(const QString &question, int page)
{
    const int budget = qMax(4000, m_config.maxContextChars);
    qint64 totalCharacters = 0;
    for (const QString &pageText : std::as_const(m_pages)) {
        totalCharacters += pageText.size();
    }

    // Embeddings are only worth it when the document does not fit in the
    // context budget. Small documents are sent in full.
    const bool needsIndex = !m_pageOnly && totalCharacters > budget;
    if (needsIndex && m_config.embeddingBackend != QLatin1String("none")) {
        maybeStartIndexing();
        if (m_indexReady && m_indexDocumentId == documentIndexKey() && m_indexModel == m_config.embeddingModel) {
            m_pendingQuestion = question;
            m_pendingPage = page;
            setBusy(true);
            m_statusLabel->setText(i18n("Searching the local index…"));
            m_queryEmbedder->setBackend(m_config.embeddingBackend);
            m_queryEmbedder->setEndpoint(m_config.embeddingEndpoint);
            m_queryEmbedder->setModel(m_config.embeddingModel);
            m_queryEmbedder->setApiKey(QString());
            m_queryEmbedder->embed(QStringList{question});
            return;
        }
    }

    QString notice;
    const QString context = buildContext(question, page, &notice);
    sendQuestion(question, context, notice);
}

void AiChatPanel::onWebSearchFinished(const QVector<WebSearchResult> &results)
{
    const QString question = m_pendingWebQuestion;
    const int page = m_pendingWebPage;
    m_pendingWebQuestion.clear();
    if (question.isEmpty()) {
        setBusy(false);
        return;
    }

    m_webContext = formatWebResults(results);
    m_statusLabel->setText(i18n("Reading the document…"));
    continueAsk(question, page);
}

void AiChatPanel::onWebSearchFailed(const QString &message)
{
    const QString question = m_pendingWebQuestion;
    const int page = m_pendingWebPage;
    m_pendingWebQuestion.clear();
    m_webContext.clear();
    if (question.isEmpty()) {
        setBusy(false);
        return;
    }

    appendSystemMessage(i18n("Web search failed: %1 Continuing with the document only.", message));
    continueAsk(question, page);
}

QString AiChatPanel::formatWebResults(const QVector<WebSearchResult> &results) const
{
    QString text = i18n("Web search results (use when relevant and cite the URL):");
    int index = 1;
    for (const WebSearchResult &result : results) {
        text += QStringLiteral("\n%1. %2\n   %3\n   %4").arg(index++).arg(result.title, result.url, result.snippet);
    }
    return text;
}

void AiChatPanel::sendQuestion(const QString &question, const QString &context, const QString &notice)
{
    QString error;
    QString apiKey = m_config.resolveApiKey(nullptr, &error);
    if (apiKey.isEmpty() && AiConfig::isLocalEndpoint(m_config.endpoint)) {
        apiKey = QStringLiteral("local");
    }
    if (apiKey.isEmpty()) {
        setBusy(false);
        appendSystemMessage(i18n("No API key available. %1", error));
        return;
    }

    QString systemContent = QStringLiteral("You are Okular, a document assistant embedded in the Okular document viewer. "
                                        "Answer questions about the document using only the excerpts provided. "
                                        "Cite pages as [page N]. If the answer is not in the excerpts, say so clearly "
                                        "instead of guessing. Be concise and use short paragraphs or bullet points.");
    if (!m_config.showThinking) {
        systemContent += QStringLiteral(" Do not include step-by-step reasoning or hidden analysis in the answer; "
                                        "respond directly.");
    }
    if (!context.isEmpty()) {
        systemContent += QStringLiteral("\n\n") + context;
    }
    if (!m_webContext.isEmpty()) {
        systemContent += QStringLiteral("\n\n") + m_webContext;
        systemContent += QStringLiteral("\n\nSome of the information above comes from the web. When you use it, "
                                        "mention the source URL inline.");
        m_webContext.clear();
    }

    QJsonArray messages;

    QJsonObject systemMessage;
    systemMessage.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMessage.insert(QStringLiteral("content"), systemContent);
    messages.append(systemMessage);

    for (const QJsonValue previous : std::as_const(m_history)) {
        messages.append(previous);
    }

    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), question);
    messages.append(userMessage);

    const int userSerial = m_nextSerial++;
    m_history.append(userMessage);
    m_historySerials.append(userSerial);
    trimHistory();

    m_pendingAssistantSerial = m_nextSerial++;
    m_transcript->addUserMessage(question, userSerial);
    m_transcript->beginAssistantMessage(m_pendingAssistantSerial);
    setBusy(true);

    m_client->setEndpoint(m_config.endpoint);
    m_client->setApiKey(apiKey);
    m_client->setModel(m_config.model);
    m_client->sendChat(messages);

    if (!notice.isEmpty()) {
        appendSystemMessage(notice);
    }
}

bool AiChatPanel::ensureDocumentText(QString *error)
{
    QObject *part = activePart();
    if (!part) {
        *error = i18n("Open a document first.");
        return false;
    }

    uint pageCount = 0;
    if (!QMetaObject::invokeMethod(part, "pages", Q_RETURN_ARG(uint, pageCount))) {
        *error = i18n("Could not read the document.");
        return false;
    }
    if (pageCount == 0) {
        *error = i18n("No document is open.");
        return false;
    }

    QString documentId;
    QMetaObject::invokeMethod(part, "currentDocument", Q_RETURN_ARG(QString, documentId));

    if (m_cachedPart == part && m_cachedDocumentId == documentId && m_cachedPageCount == int(pageCount) && m_pages.size() == int(pageCount)) {
        return true;
    }

    m_pages.clear();
    m_pages.reserve(int(pageCount));

    qint64 usedCharacters = 0;
    for (uint page = 1; page <= pageCount; ++page) {
        QString text;
        if (QMetaObject::invokeMethod(part, "pageText", Q_RETURN_ARG(QString, text), Q_ARG(uint, page))) {
            if (usedCharacters + text.size() > kMaxExtractedCharacters) {
                text.truncate(int(qMax<qint64>(0, kMaxExtractedCharacters - usedCharacters)));
            }
            usedCharacters += text.size();
            m_pages.append(text);
        } else {
            m_pages.append(QString());
        }

        if ((page % 16) == 0) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }

    QString title;
    QMetaObject::invokeMethod(part, "documentMetaData", Q_RETURN_ARG(QString, title), Q_ARG(QString, QStringLiteral("Title")));
    m_documentTitle = title.trimmed();

    m_cachedPart = part;
    m_cachedDocumentId = documentId;
    m_cachedPageCount = int(pageCount);
    return true;
}

int AiChatPanel::documentPageCount() const
{
    QObject *part = activePart();
    if (!part) {
        return 0;
    }
    uint pages = 0;
    QMetaObject::invokeMethod(part, "pages", Q_RETURN_ARG(uint, pages));
    return int(pages);
}

int AiChatPanel::currentPageNumber() const
{
    QObject *part = activePart();
    if (!part) {
        return 0;
    }
    uint page = 0;
    QMetaObject::invokeMethod(part, "currentPage", Q_RETURN_ARG(uint, page));
    return int(page);
}

QString AiChatPanel::documentName() const
{
    if (!m_documentTitle.isEmpty()) {
        return m_documentTitle;
    }

    QObject *part = activePart();
    if (!part) {
        return i18n("Untitled");
    }

    QString title;
    QMetaObject::invokeMethod(part, "documentMetaData", Q_RETURN_ARG(QString, title), Q_ARG(QString, QStringLiteral("Title")));
    if (!title.trimmed().isEmpty()) {
        return title.trimmed();
    }

    QString location;
    QMetaObject::invokeMethod(part, "currentDocument", Q_RETURN_ARG(QString, location));
    const QString name = location.mid(location.lastIndexOf(QLatin1Char('/')) + 1);
    return name.isEmpty() ? i18n("Untitled") : name;
}

QStringList AiChatPanel::queryTerms(const QString &question)
{
    static const QSet<QString> stopWords = {
        QStringLiteral("the"),   QStringLiteral("and"),  QStringLiteral("for"),    QStringLiteral("are"),
        QStringLiteral("but"),   QStringLiteral("not"),  QStringLiteral("you"),    QStringLiteral("this"),
        QStringLiteral("that"),  QStringLiteral("with"), QStringLiteral("what"),   QStringLiteral("which"),
        QStringLiteral("how"),   QStringLiteral("why"),  QStringLiteral("when"),   QStringLiteral("where"),
        QStringLiteral("from"),  QStringLiteral("into"), QStringLiteral("does"),   QStringLiteral("did"),
        QStringLiteral("about"), QStringLiteral("please"), QStringLiteral("explain"), QStringLiteral("summarize"),
        QStringLiteral("document"), QStringLiteral("page"), QStringLiteral("tell"), QStringLiteral("give"),
    };

    static const QRegularExpression wordPattern(QStringLiteral("[\\p{L}\\p{N}][\\p{L}\\p{N}_-]+"));

    QStringList terms;
    QSet<QString> seen;
    auto it = wordPattern.globalMatch(question.toLower());
    while (it.hasNext()) {
        const QString term = it.next().captured();
        if (term.size() < 3 || stopWords.contains(term) || seen.contains(term)) {
            continue;
        }
        seen.insert(term);
        terms.append(term);
    }
    return terms;
}

QString AiChatPanel::buildContext(const QString &question, int currentPageNumber, QString *notice) const
{
    if (m_pages.isEmpty()) {
        return QString();
    }

    const int budget = qMax(4000, m_config.maxContextChars);
    const QString title = m_documentTitle.isEmpty() ? documentName() : m_documentTitle;

    QStringList lines;
    lines.append(i18n("Document: %1", title));
    lines.append(i18n("Pages: %1", static_cast<int>(m_pages.size())));
    lines.append(m_pageOnly ? i18n("Scope: current page") : i18n("Scope: whole document"));
    lines.append(QString());
    int used = static_cast<int>(lines.join(QLatin1Char('\n')).size());

    if (m_pageOnly) {
        const int pageIndex = currentPageNumber - 1;
        if (pageIndex >= 0 && pageIndex < m_pages.size()) {
            QString text = m_pages.at(pageIndex).trimmed();
            const int available = qMax(0, budget - used);
            if (available > 0 && text.size() > available) {
                text.truncate(available);
            }
            lines.append(i18n("--- Page %1 ---", currentPageNumber));
            lines.append(text);
            if (notice) {
                *notice = i18n("Scope: current page only.");
            }
            return lines.join(QLatin1Char('\n'));
        }
    }

    qint64 totalCharacters = 0;
    for (const QString &pageText : m_pages) {
        totalCharacters += pageText.size();
    }

    if (totalCharacters <= budget) {
        for (int index = 0; index < m_pages.size(); ++index) {
            const QString text = m_pages.at(index).trimmed();
            if (text.isEmpty()) {
                continue;
            }
            lines.append(i18n("--- Page %1 ---", index + 1));
            lines.append(text);
            lines.append(QString());
        }
        return lines.join(QLatin1Char('\n'));
    }

    struct Chunk {
        int page = 0;
        int order = 0;
        QString text;
        int score = 0;
    };

    QVector<Chunk> chunks;
    for (int pageIndex = 0; pageIndex < m_pages.size(); ++pageIndex) {
        const QString &pageText = m_pages.at(pageIndex);
        int position = 0;
        while (position < pageText.size()) {
            int end = qMin(position + kChunkSize, static_cast<int>(pageText.size()));
            if (end < pageText.size()) {
                const int newline = pageText.lastIndexOf(QLatin1Char('\n'), end);
                if (newline > position + kChunkSize / 2) {
                    end = newline;
                }
            }

            Chunk chunk;
            chunk.page = pageIndex + 1;
            chunk.order = static_cast<int>(chunks.size());
            chunk.text = pageText.mid(position, end - position).trimmed();
            if (!chunk.text.isEmpty()) {
                chunks.append(chunk);
            }

            if (end >= pageText.size()) {
                break;
            }
            position = qMax(end - kChunkOverlap, position + 1);
        }
    }

    const QStringList terms = queryTerms(question);
    for (Chunk &chunk : chunks) {
        for (const QString &term : terms) {
            chunk.score += chunk.text.count(term, Qt::CaseInsensitive);
        }
        if (chunk.page == currentPageNumber) {
            chunk.score += 50;
        }
    }

    const bool overviewRequest = question.contains(QStringLiteral("summar"), Qt::CaseInsensitive)
        || question.contains(QStringLiteral("overview"), Qt::CaseInsensitive)
        || question.contains(QStringLiteral("main topics"), Qt::CaseInsensitive);

    QVector<int> selected;
    int longestChunk = 0;
    for (const Chunk &chunk : std::as_const(chunks)) {
        longestChunk = qMax(longestChunk, static_cast<int>(chunk.text.size()) + 24);
    }
    const int chunkCost = qMax(400, longestChunk);

    if (overviewRequest || terms.isEmpty()) {
        const int targetCount = qMax(1, (budget - used) / chunkCost);
        const int step = qMax(1, static_cast<int>(chunks.size()) / targetCount);
        for (int index = 0; index < chunks.size(); index += step) {
            selected.append(index);
        }
        if (selected.size() > targetCount) {
            selected.resize(targetCount);
        }
    } else {
        QVector<int> candidates;
        candidates.reserve(chunks.size());
        for (int index = 0; index < chunks.size(); ++index) {
            candidates.append(index);
        }
        std::sort(candidates.begin(), candidates.end(), [&chunks](int left, int right) {
            if (chunks.at(left).score != chunks.at(right).score) {
                return chunks.at(left).score > chunks.at(right).score;
            }
            return chunks.at(left).order < chunks.at(right).order;
        });

        int selectionUsed = used;
        for (int index : std::as_const(candidates)) {
            if (chunks.at(index).score <= 0) {
                break;
            }
            const int cost = static_cast<int>(chunks.at(index).text.size()) + 24;
            if (selectionUsed + cost > budget) {
                continue;
            }
            selected.append(index);
            selectionUsed += cost;
        }

        if (selected.isEmpty()) {
            const int targetCount = qMax(1, (budget - used) / chunkCost);
            const int step = qMax(1, static_cast<int>(chunks.size()) / targetCount);
            for (int index = 0; index < chunks.size(); index += step) {
                selected.append(index);
                if (selected.size() >= targetCount) {
                    break;
                }
            }
        }
    }

    // Make sure the page the user is looking at is represented.
    if (currentPageNumber > 0) {
        bool hasCurrentPage = false;
        for (int index : std::as_const(selected)) {
            if (chunks.at(index).page == currentPageNumber) {
                hasCurrentPage = true;
                break;
            }
        }
        if (!hasCurrentPage) {
            for (int index = 0; index < chunks.size(); ++index) {
                if (chunks.at(index).page == currentPageNumber
                    && used + static_cast<int>(chunks.at(index).text.size()) + 24 <= budget) {
                    selected.append(index);
                    used += static_cast<int>(chunks.at(index).text.size()) + 24;
                    break;
                }
            }
        }
    }

    std::sort(selected.begin(), selected.end());

    for (int index : std::as_const(selected)) {
        const Chunk &chunk = chunks.at(index);
        const int chunkCostWithHeader = static_cast<int>(chunk.text.size()) + 24;
        if (used + chunkCostWithHeader > budget) {
            continue;
        }
        lines.append(i18n("--- Page %1 ---", chunk.page));
        lines.append(chunk.text);
        lines.append(QString());
        used += chunkCostWithHeader;
    }

    if (notice) {
        *notice = i18n("This is a long document, so only the most relevant excerpts were sent to the model.");
    }

    return lines.join(QLatin1Char('\n'));
}

void AiChatPanel::updateStatus()
{
    const int pageCount = documentPageCount();
    const QString name = documentName();

    if (m_documentLabel) {
        if (pageCount <= 0) {
            m_documentLabel->setText(i18n("No document open"));
            m_documentLabel->setToolTip(QString());
        } else {
            const QString pages = i18np("%1 page", "%1 pages", pageCount);
            m_documentLabel->setText(name.isEmpty() ? pages : i18n("%1 · %2", name, pages));
            m_documentLabel->setToolTip(name);
        }
    }

    QString keySource;
    QString keyError;
    m_config.resolveApiKey(&keySource, &keyError);

    QString keyPart;
    const bool localEndpoint = AiConfig::isLocalEndpoint(m_config.endpoint);
    if (!keyError.isEmpty() && !localEndpoint) {
        keyPart = i18n("no API key");
    } else if (keySource.isEmpty()) {
        if (m_config.provider == QLatin1String("ollama")) {
            keyPart = i18n("Ollama");
        } else if (m_config.provider == QLatin1String("llamacpp")) {
            keyPart = i18n("llama.cpp");
        } else {
            keyPart = i18n("local");
        }
    } else {
        keyPart = i18n("key: %1", keySource);
    }

    if (m_setupBanner) {
        QString bannerError;
        const bool hasKey = !m_config.resolveApiKey(nullptr, &bannerError).isEmpty();
        m_setupBanner->setVisible(!hasKey && !localEndpoint);
    }

    QStringList parts;
    parts << m_config.model;
    parts << (m_pageOnly ? i18n("this page") : i18n("full document"));
    if (m_config.webSearchEnabled) {
        parts << i18n("web search");
    }
    parts << keyPart;
    if (m_config.embeddingBackend != QLatin1String("none")) {
        parts << embeddingStatus();
    }
    m_statusLabel->setText(parts.join(QStringLiteral(" · ")));

    QString tooltip = m_config.endpoint;
    if (m_config.embeddingBackend != QLatin1String("none")) {
        tooltip += QLatin1Char('\n') + i18n("Embeddings: %1 at %2", m_config.embeddingModel, m_config.embeddingEndpoint);
    }
    if (!keyError.isEmpty() && !localEndpoint) {
        tooltip += QLatin1Char('\n') + keyError;
    }
    if (!m_indexError.isEmpty()) {
        tooltip += QLatin1Char('\n') + m_indexError;
    }
    m_statusLabel->setToolTip(tooltip);
}

void AiChatPanel::applyTheme()
{
    if (m_applyingTheme) {
        return;
    }
    m_applyingTheme = true;

    const QPalette p = palette();
    const QColor window = p.color(QPalette::Window);
    const QColor text = p.color(QPalette::WindowText);
    const QColor base = p.color(QPalette::Base);
    const bool dark = window.lightness() < 128;
    const QColor surface = dark ? window.lighter(115) : base.darker(102);
    const QColor hover = dark ? window.lighter(125) : window.darker(106);
    const QColor border = p.color(QPalette::Mid);
    const QColor muted = p.color(QPalette::Disabled, QPalette::WindowText);
    const QColor accent = p.color(QPalette::Highlight);
    const QColor accentText = p.color(QPalette::HighlightedText);
    const QColor accentHover = dark ? accent.lighter(110) : accent.darker(110);

    auto rgba = [](const QColor &color, int alpha) {
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(alpha);
    };

    QString css = QStringLiteral(R"(
#okular_ai_panel { background: @window; }
QLabel#aiTitle { color: @text; }
QLabel#aiDocumentLabel { color: @text; }
QLabel#aiStatus { color: @muted; }
QLabel#bubbleMeta { color: @muted; }
QLabel#chatSystem { color: @muted; }
QLabel#emptyTitle { color: @text; }
QLabel#emptySubtitle { color: @muted; }
QFrame#composer { background: @surface; border: 1px solid @border; border-radius: 16px; }
QFrame#chatBubbleAssistant { background: transparent; border: none; }
QFrame#chatBubbleAssistant QLabel#bubbleContent { color: @text; }
QFrame#chatBubbleUser { background: @accent; border: none; border-radius: 16px; }
QFrame#chatBubbleUser QLabel#bubbleContent { color: @accentText; }
QFrame#chatBubbleUser QLabel#bubbleMeta { color: @accentMeta; }
QToolButton#messageAction { border: none; border-radius: 6px; padding: 2px; background: transparent; }
QToolButton#messageAction:hover { background: @hover; }
QToolButton#aiHeaderToggle { border: none; border-radius: 8px; background: transparent; padding: 2px 8px; color: @muted; }
QToolButton#aiHeaderToggle:hover { background: @hover; color: @text; }
QToolButton#aiHeaderToggle:checked { background: @accentSoft; color: @text; }
QWidget#thinkingBox { background: @surface; border: 1px solid @border; border-radius: 10px; }
QToolButton#thinkingToggle { border: none; background: transparent; color: @muted; padding: 0; text-align: left; }
QToolButton#thinkingToggle:hover { color: @text; }
QLabel#thinkingContent { color: @muted; }
QFrame#aiSetupBanner { background: @accentSoft; border: 1px solid @border; border-radius: 12px; }
QLabel#setupBannerText { color: @text; }
QPushButton#setupBannerButton { background: @accent; color: @accentText; border: none; border-radius: 9px; padding: 7px 12px; font-weight: 600; }
QPushButton#setupBannerButton:hover { background: @accentHover; }
QPushButton#suggestionButton { background: @surface; border: 1px solid @border; border-radius: 12px; padding: 10px 12px; color: @text; text-align: left; }
QPushButton#suggestionButton:hover { background: @hover; }
QPushButton#scopeButton { background: transparent; border: 1px solid @border; border-radius: 9px; padding: 6px 14px; min-height: 28px; color: @muted; }
QPushButton#scopeButton:hover { background: @hover; }
QPushButton#scopeButton:checked { background: @accent; color: @accentText; border-color: @accent; }
QPushButton#sendButton { background: @accent; color: @accentText; border: none; border-radius: 10px; padding: 8px 18px; font-weight: 600; }
QPushButton#sendButton:hover { background: @accentHover; }
QPushButton#sendButton:disabled { background: @border; color: @muted; }
QPushButton#stopButton { background: transparent; border: 1px solid @border; border-radius: 9px; padding: 6px 14px; color: @text; }
QPushButton#stopButton:hover { background: @hover; }
QComboBox#aiModelCombo { background: @surface; border: 1px solid @border; border-radius: 8px; padding: 3px 10px; min-height: 26px; color: @text; }
QToolButton { border: none; border-radius: 6px; padding: 5px; }
QToolButton:hover { background: @hover; }
QPlainTextEdit#aiInput { background: transparent; border: none; color: @text; selection-background-color: @accent; selection-color: @accentText; }
QScrollArea#aiTranscript { background: transparent; border: none; }
QWidget#aiTranscriptContent, QWidget#aiMessages, QWidget#aiEmptyState { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: @border; min-height: 24px; border-radius: 4px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)");

    css.replace(QStringLiteral("@window"), window.name());
    css.replace(QStringLiteral("@text"), text.name());
    css.replace(QStringLiteral("@muted"), rgba(muted, 200));
    css.replace(QStringLiteral("@surface"), surface.name());
    css.replace(QStringLiteral("@hover"), hover.name());
    css.replace(QStringLiteral("@border"), rgba(border, 110));
    css.replace(QStringLiteral("@accentSoft"), rgba(accent, dark ? 60 : 35));
    css.replace(QStringLiteral("@accentMeta"), rgba(accentText, 180));
    css.replace(QStringLiteral("@accentHover"), accentHover.name());
    css.replace(QStringLiteral("@accentText"), accentText.name());
    css.replace(QStringLiteral("@accent"), accent.name());

    setStyleSheet(css);
    m_applyingTheme = false;
}

void AiChatPanel::updateModelCombo()
{
    if (!m_modelCombo) {
        return;
    }
    const QSignalBlocker blocker(m_modelCombo);
    m_modelCombo->clear();

    QStringList models;
    if (m_config.provider == QLatin1String("deepseek")) {
        models = {QStringLiteral("deepseek-flash"), QStringLiteral("deepseek-v4-pro")};
    }
    // Local providers keep the discovered model list in AI settings; the panel
    // only needs the currently selected one.
    if (!m_config.model.isEmpty() && !models.contains(m_config.model)) {
        models.prepend(m_config.model);
    }
    for (const QString &model : std::as_const(models)) {
        m_modelCombo->addItem(model, model);
    }
    const int index = m_modelCombo->findData(m_config.model);
    if (index >= 0) {
        m_modelCombo->setCurrentIndex(index);
    }
}

void AiChatPanel::goToPage(int page)
{
    QObject *part = activePart();
    if (!part || page <= 0) {
        return;
    }
    QMetaObject::invokeMethod(part, "goToPage", Q_ARG(uint, static_cast<uint>(page)));
}

void AiChatPanel::editMessage(int historySerial, const QString &text)
{
    if (m_input) {
        m_input->setPlainText(text);
        QTextCursor cursor = m_input->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_input->setTextCursor(cursor);
        focusComposer();
    }

    // While a reply is being generated, just quote the message instead of
    // rewriting the conversation.
    if (m_client->isBusy() || m_queryEmbedder->isBusy()) {
        return;
    }

    const int index = m_historySerials.indexOf(historySerial);
    if (index < 0) {
        return;
    }

    while (m_history.size() > index) {
        m_history.removeAt(m_history.size() - 1);
    }
    while (m_historySerials.size() > index) {
        m_historySerials.removeAt(m_historySerials.size() - 1);
    }
    m_transcript->removeMessagesFromSerial(historySerial);
    updateStatus();
}

void AiChatPanel::appendSystemMessage(const QString &text)
{
    if (m_transcript) {
        m_transcript->addSystemMessage(text);
    }
}

void AiChatPanel::appendUserMessage(const QString &text)
{
    if (m_transcript) {
        m_transcript->addUserMessage(text);
    }
}

void AiChatPanel::setBusy(bool busy)
{
    m_sendButton->setEnabled(!busy);
    m_input->setEnabled(!busy);
    m_modelCombo->setEnabled(!busy);
    m_stopButton->setVisible(busy);
    if (!busy) {
        updateStatus();
    }
}

void AiChatPanel::trimHistory()
{
    while (m_history.size() > 20) {
        m_history.removeAt(0);
        if (!m_historySerials.isEmpty()) {
            m_historySerials.removeAt(0);
        }
    }
}

void AiChatPanel::clearConversation()
{
    if (!m_transcript->isEmpty()) {
        const auto answer = QMessageBox::question(this,
                                                  i18n("Clear conversation"),
                                                  i18n("Clear this conversation? This cannot be undone."),
                                                  QMessageBox::Yes | QMessageBox::No,
                                                  QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    m_history = QJsonArray();
    m_historySerials.clear();
    m_pendingAssistantSerial = -1;
    m_webContext.clear();
    m_pendingWebQuestion.clear();
    m_transcript->clear();
    updateStatus();
}

void AiChatPanel::openSettings()
{
    showSettings(false);
}

void AiChatPanel::openLocalSetup()
{
    showSettings(true);
}

void AiChatPanel::showSettings(bool preferLocal)
{
    QDialog dialog(this);
    dialog.setWindowTitle(i18n("AI settings"));
    dialog.setMinimumWidth(520);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *intro = new QLabel(i18n("Choose a provider. Local models are free, private and need no API key."), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *providerCombo = new QComboBox(&dialog);
    providerCombo->addItem(i18n("DeepSeek (cloud)"), QStringLiteral("deepseek"));
    providerCombo->addItem(i18n("Ollama (local, free)"), QStringLiteral("ollama"));
    providerCombo->addItem(i18n("llama.cpp (local, free)"), QStringLiteral("llamacpp"));
    providerCombo->addItem(i18n("Custom OpenAI-compatible"), QStringLiteral("custom"));

    auto *modelCombo = new QComboBox(&dialog);
    modelCombo->setEditable(true);
    modelCombo->setInsertPolicy(QComboBox::NoInsert);
    modelCombo->lineEdit()->setPlaceholderText(i18n("Choose a detected model or type a name"));

    auto *probeLabel = new QLabel(&dialog);
    probeLabel->setWordWrap(true);
    probeLabel->setOpenExternalLinks(true);
    probeLabel->setObjectName(QStringLiteral("probeLabel"));
    auto *detectButton = new QPushButton(i18n("Detect models"), &dialog);

    auto *simpleForm = new QFormLayout;
    simpleForm->addRow(i18n("Provider:"), providerCombo);
    simpleForm->addRow(i18n("Model:"), modelCombo);
    auto *detectRow = new QWidget(&dialog);
    auto *detectLayout = new QHBoxLayout(detectRow);
    detectLayout->setContentsMargins(0, 0, 0, 0);
    detectLayout->addWidget(detectButton);
    detectLayout->addWidget(probeLabel, 1);
    simpleForm->addRow(QString(), detectRow);

    auto *thinkingCheck = new QCheckBox(i18n("Show the model's thinking"), &dialog);
    thinkingCheck->setChecked(m_config.showThinking);
    thinkingCheck->setToolTip(i18n("When off, reasoning is hidden and the model is asked to answer directly."));
    simpleForm->addRow(QString(), thinkingCheck);
    layout->addLayout(simpleForm);

    auto *form = new QFormLayout;

    auto *keyFileEdit = new QLineEdit(m_config.apiKeyFile, &dialog);
    const QString detectedPath = AiConfig::findDefaultKeyFile();
    keyFileEdit->setPlaceholderText(detectedPath.isEmpty() ? i18n("Path to a file containing your API key") : QDir::toNativeSeparators(detectedPath));

    auto *browseButton = new QPushButton(i18n("Browse…"), &dialog);
    auto *keyFileRow = new QWidget(&dialog);
    auto *keyFileLayout = new QHBoxLayout(keyFileRow);
    keyFileLayout->setContentsMargins(0, 0, 0, 0);
    keyFileLayout->addWidget(keyFileEdit);
    keyFileLayout->addWidget(browseButton);
    form->addRow(i18n("API key file:"), keyFileRow);

    auto *apiKeyEdit = new QLineEdit(&dialog);
    apiKeyEdit->setEchoMode(QLineEdit::Password);
    apiKeyEdit->setPlaceholderText(i18n("Optional — overrides the key file for this session"));
    form->addRow(i18n("API key:"), apiKeyEdit);

    auto *saveKeyCheck = new QCheckBox(i18n("Store the API key in the config file (plain text)"), &dialog);
    saveKeyCheck->setChecked(m_config.saveKey);
    form->addRow(QString(), saveKeyCheck);

    auto *endpointEdit = new QLineEdit(m_config.endpoint, &dialog);
    form->addRow(i18n("Endpoint:"), endpointEdit);

    auto *contextSpin = new QSpinBox(&dialog);
    contextSpin->setRange(4000, 400000);
    contextSpin->setSingleStep(4000);
    contextSpin->setSuffix(i18n(" characters"));
    contextSpin->setValue(m_config.maxContextChars);
    form->addRow(i18n("Document context budget:"), contextSpin);

    auto *embeddingBackendCombo = new QComboBox(&dialog);
    embeddingBackendCombo->addItem(i18n("Ollama (local)"), QStringLiteral("ollama"));
    embeddingBackendCombo->addItem(i18n("OpenAI-compatible"), QStringLiteral("openai"));
    embeddingBackendCombo->addItem(i18n("Disabled (keyword search)"), QStringLiteral("none"));
    const int currentBackend = embeddingBackendCombo->findData(m_config.embeddingBackend);
    embeddingBackendCombo->setCurrentIndex(currentBackend >= 0 ? currentBackend : 0);
    form->addRow(i18n("Embeddings:"), embeddingBackendCombo);

    auto *embeddingEndpointEdit = new QLineEdit(m_config.embeddingEndpoint, &dialog);
    form->addRow(i18n("Embedding endpoint:"), embeddingEndpointEdit);

    auto *embeddingModelEdit = new QLineEdit(m_config.embeddingModel, &dialog);
    embeddingModelEdit->setPlaceholderText(QStringLiteral("all-minilm"));
    form->addRow(i18n("Embedding model:"), embeddingModelEdit);

    auto *embeddingHint = new QLabel(i18n("For Ollama, install a small model with: ollama pull all-minilm. "
                                           "Embeddings are used only for documents that do not fit in the context budget."),
                                     &dialog);
    embeddingHint->setWordWrap(true);
    form->addRow(QString(), embeddingHint);

    auto *advanced = new QGroupBox(i18n("Advanced settings"), &dialog);
    advanced->setCheckable(true);
    advanced->setChecked(false);
    advanced->setLayout(form);
    layout->addWidget(advanced);

    auto *hint = new QLabel(&dialog);
    hint->setWordWrap(true);
    hint->setText(i18n("An API key is only needed for cloud providers. It is never written to the source tree "
                       "and can also be supplied through OKULAR_AI_API_KEY, DEEPSEEK_API_KEY or OPENAI_API_KEY."));
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    auto *catalog = new ModelCatalog(&dialog);

    auto presetEndpoint = [](const QString &provider) -> QString {
        if (provider == QLatin1String("ollama")) {
            return ModelCatalog::defaultOllamaChatEndpoint();
        }
        if (provider == QLatin1String("llamacpp")) {
            return ModelCatalog::defaultLlamaCppChatEndpoint();
        }
        if (provider == QLatin1String("deepseek")) {
            return QStringLiteral("https://api.deepseek.com/chat/completions");
        }
        return QString();
    };

    auto presetModels = [](const QString &provider) -> QStringList {
        if (provider == QLatin1String("deepseek")) {
            return {QStringLiteral("deepseek-flash"), QStringLiteral("deepseek-v4-pro")};
        }
        return {};
    };

    auto replaceModels = [&](const QStringList &models, const QString &selected) {
        const QSignalBlocker blocker(modelCombo);
        modelCombo->clear();
        modelCombo->addItems(models);
        if (!selected.isEmpty()) {
            const int index = modelCombo->findText(selected);
            if (index >= 0) {
                modelCombo->setCurrentIndex(index);
            } else {
                modelCombo->setEditText(selected);
            }
        } else if (modelCombo->count() > 0) {
            modelCombo->setCurrentIndex(0);
        }
    };

    auto localBase = [&]() -> QString {
        const QString text = endpointEdit->text().trimmed();
        QUrl url(text);
        if (url.scheme().isEmpty()) {
            url = QUrl(QStringLiteral("http://") + text);
        }
        if (!url.isValid() || url.host().isEmpty()) {
            return QString();
        }
        QString base = url.scheme() + QStringLiteral("://") + url.host();
        if (url.port() > 0) {
            base += QLatin1Char(':') + QString::number(url.port());
        }
        return base;
    };

    auto runProbe = [&]() {
        const QString provider = providerCombo->currentData().toString();
        const QString base = localBase();
        if (provider == QLatin1String("ollama")) {
            probeLabel->setText(i18n("Looking for Ollama…"));
            catalog->probeOllama(base.isEmpty() ? QStringLiteral("http://127.0.0.1:11434") : base);
        } else if (provider == QLatin1String("llamacpp")) {
            probeLabel->setText(i18n("Looking for a local llama.cpp server…"));
            catalog->probeLlamaCpp(base.isEmpty() ? QStringLiteral("http://127.0.0.1:8080") : base);
        } else if (provider == QLatin1String("custom")) {
            if (AiConfig::isLocalEndpoint(endpointEdit->text()) && !base.isEmpty()) {
                probeLabel->setText(i18n("Looking for a local server…"));
                catalog->probeLlamaCpp(base);
            } else {
                probeLabel->setText(i18n("Enter the model name used by your server."));
            }
        } else {
            probeLabel->setText(i18n("Cloud provider selected. Add an API key under Advanced settings."));
        }
    };

    connect(detectButton, &QPushButton::clicked, &dialog, runProbe);
    connect(catalog, &ModelCatalog::modelsFound, &dialog, [&](int source, const QStringList &models) {
        if (models.isEmpty()) {
            if (source == ModelCatalog::Ollama) {
                probeLabel->setText(i18n("Ollama is running but has no models. Install one with: ollama pull qwen2.5:1.5b"));
            } else {
                probeLabel->setText(i18n("The local server reported no loaded models."));
            }
            return;
        }
        const QString current = modelCombo->currentText().trimmed();
        const QString selected = models.contains(current) ? current : models.first();
        replaceModels(models, selected);
        if (source == ModelCatalog::Ollama) {
            probeLabel->setText(i18np("%1 Ollama model found.", "%1 Ollama models found.", models.size()).arg(models.size()));
        } else {
            probeLabel->setText(i18np("%1 local model found.", "%1 local models found.", models.size()).arg(models.size()));
        }
    });
    connect(catalog, &ModelCatalog::probeFailed, &dialog, [&](int source, const QString &message) {
        Q_UNUSED(message)
        if (source == ModelCatalog::Ollama) {
            probeLabel->setText(i18n("Ollama was not detected. Install it from "
                                     "<a href=\"https://ollama.com\">ollama.com</a>, then run "
                                     "<b>ollama pull qwen2.5:1.5b</b> and press Detect again."));
        } else {
            probeLabel->setText(i18n("No local llama.cpp server was found. Start it with "
                                     "<b>llama-server -m model.gguf --port 8080</b>, then press Detect again."));
        }
    });
    connect(providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&](int) {
        const QString provider = providerCombo->currentData().toString();
        advanced->setChecked(provider != QLatin1String("ollama") && provider != QLatin1String("llamacpp"));
        const QString endpoint = presetEndpoint(provider);
        if (!endpoint.isEmpty()) {
            endpointEdit->setText(endpoint);
        }
        const QStringList models = presetModels(provider);
        if (!models.isEmpty()) {
            const QString selected = models.contains(m_config.model) ? m_config.model : models.first();
            replaceModels(models, selected);
        } else if (provider == QLatin1String("ollama") || provider == QLatin1String("llamacpp") || provider == QLatin1String("custom")) {
            replaceModels({}, QString());
        }
        runProbe();
    });

    QString initialProvider = preferLocal ? QStringLiteral("ollama") : m_config.provider;
    int providerIndex = providerCombo->findData(initialProvider);
    if (providerIndex < 0) {
        providerIndex = 0;
    }
    {
        const QSignalBlocker blocker(providerCombo);
        providerCombo->setCurrentIndex(providerIndex);
    }
    initialProvider = providerCombo->currentData().toString();
    advanced->setChecked(initialProvider == QLatin1String("deepseek") || initialProvider == QLatin1String("custom"));

    QStringList initialModels = presetModels(initialProvider);
    if (initialModels.isEmpty() && !m_config.model.isEmpty() && !preferLocal && m_config.provider == initialProvider) {
        initialModels << m_config.model;
    }
    const QString initialModel = initialModels.contains(m_config.model) ? m_config.model : (initialModels.isEmpty() ? QString() : initialModels.first());
    replaceModels(initialModels, initialModel);

    const QString initialEndpoint = presetEndpoint(initialProvider);
    if (preferLocal && !initialEndpoint.isEmpty()) {
        endpointEdit->setText(initialEndpoint);
    } else if (!m_config.endpoint.isEmpty()) {
        endpointEdit->setText(m_config.endpoint);
    }

    runProbe();

    connect(browseButton, &QPushButton::clicked, &dialog, [&dialog, keyFileEdit]() {
        const QString file = QFileDialog::getOpenFileName(&dialog, i18n("Select API Key File"), keyFileEdit->text());
        if (!file.isEmpty()) {
            keyFileEdit->setText(file);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (modelCombo->currentText().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog,
                                 i18n("Model required"),
                                 i18n("Choose a detected model or type the model name.\n\nFor Ollama you can install a small model with:\nollama pull qwen2.5:1.5b"));
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_config.provider = providerCombo->currentData().toString();
    m_config.apiKeyFile = keyFileEdit->text().trimmed();
    if (!apiKeyEdit->text().trimmed().isEmpty()) {
        m_config.apiKey = apiKeyEdit->text().trimmed();
    }
    m_config.saveKey = saveKeyCheck->isChecked();
    if (!endpointEdit->text().trimmed().isEmpty()) {
        m_config.endpoint = endpointEdit->text().trimmed();
    }
    if (!modelCombo->currentText().trimmed().isEmpty()) {
        m_config.model = modelCombo->currentText().trimmed();
    }
    m_config.maxContextChars = contextSpin->value();
    m_config.showThinking = thinkingCheck->isChecked();
    if (m_thinkingButton) {
        const QSignalBlocker blocker(m_thinkingButton);
        m_thinkingButton->setChecked(m_config.showThinking);
    }

    const QString newBackend = embeddingBackendCombo->currentData().toString();
    const QString newEndpoint = embeddingEndpointEdit->text().trimmed();
    const QString newModel = embeddingModelEdit->text().trimmed();
    const bool embeddingChanged = m_config.embeddingBackend != newBackend
        || m_config.embeddingEndpoint != newEndpoint
        || m_config.embeddingModel != newModel;

    m_config.embeddingBackend = newBackend;
    if (!newEndpoint.isEmpty()) {
        m_config.embeddingEndpoint = newEndpoint;
    }
    if (!newModel.isEmpty()) {
        m_config.embeddingModel = newModel;
    }

    if (embeddingChanged || !m_indexError.isEmpty()) {
        m_indexEmbedder->abort();
        m_queryEmbedder->abort();
        m_pendingQuestion.clear();
        m_indexing = false;
        m_indexReady = false;
        m_index.clear();
        m_indexDocumentId.clear();
        m_indexError.clear();
        m_indexErrorShown = false;
    }

    m_config.save();
    updateModelCombo();
    updateStatus();
}
