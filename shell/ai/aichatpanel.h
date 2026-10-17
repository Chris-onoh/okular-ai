/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_AI_AICHATPANEL_H
#define OKULAR_AI_AICHATPANEL_H

#include "aiconfig.h"
#include "websearchclient.h"

#include <QJsonArray>
#include <QList>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class LlmClient;
class EmbeddingClient;
class ModelCatalog;
class ChatTranscript;
class QLabel;
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QToolButton;
class QFrame;

/**
 * Chat panel that answers questions about the document currently open in
 * the active Okular tab.
 *
 * The panel talks to the KParts part through QMetaObject::invokeMethod()
 * (pages(), currentPage(), documentMetaData(), pageText()), so it does not
 * need to link against the part library.
 */
class AiChatPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AiChatPanel(QWidget *parent = nullptr);
    ~AiChatPanel() override;

    void setActivePartProvider(std::function<QObject *()> provider);

    /**
     * Called when the active tab or its document changes. Drops
     * cached page text.
     */
    void documentChanged();

    /**
     * Focuses the message composer.
     */
    void focusComposer();

    /**
     * Runs a ready-made prompt from elsewhere in the application, e.g. the
     * page context menu. Does nothing while an answer is being generated.
     */
    void askFromOutside(const QString &prompt);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private Q_SLOTS:
    void sendMessage();
    void stopGeneration();
    void openSettings();
    void openLocalSetup();
    void clearConversation();
    void summarizeDocument();
    void explainCurrentPage();

    void goToPage(int page);
    void editMessage(int historySerial, const QString &text);

private:
    struct IndexedChunk {
        int page = 0;
        int order = 0;
        QString text;
        QVector<float> embedding;
    };

    void ask(const QString &question);
    void continueAsk(const QString &question, int page);
    void onWebSearchFinished(const QVector<WebSearchResult> &results);
    void onWebSearchFailed(const QString &message);
    QString formatWebResults(const QVector<WebSearchResult> &results) const;
    void sendQuestion(const QString &question, const QString &context, const QString &notice);
    QObject *activePart() const;
    bool ensureDocumentText(QString *error);
    void invalidateDocumentCache();
    QString buildContext(const QString &question, int currentPageNumber, QString *notice) const;
    QString buildContextFromIndex(const QString &question, const QVector<float> &queryEmbedding, int currentPageNumber, QString *notice) const;
    void maybeStartIndexing();
    void startIndexing();
    void requestNextEmbeddingBatch();
    void finishIndexing(bool success, const QString &error);
    QVector<IndexedChunk> makeIndexChunks() const;
    QString documentIndexKey() const;
    QString embeddingStatus() const;
    static QVector<float> normalized(const QVector<float> &vector);
    static float dotProduct(const QVector<float> &left, const QVector<float> &right);
    void showSettings(bool preferLocal);
    void updateStatus();
    void applyTheme();
    void refreshThinkingCapability();
    void applyThinkingControls();
    void updateModelCombo();
    void appendSystemMessage(const QString &text);
    void appendUserMessage(const QString &text);
    void setBusy(bool busy);
    void trimHistory();

    QString documentName() const;
    int currentPageNumber() const;
    int documentPageCount() const;
    static QStringList queryTerms(const QString &question);

    AiConfig m_config;
    LlmClient *m_client;
    EmbeddingClient *m_indexEmbedder;
    EmbeddingClient *m_queryEmbedder;
    ModelCatalog *m_catalog = nullptr;
    int m_thinkingSupport = -1; // 1 = supported, 0 = unsupported, -1 = unknown
    std::function<QObject *()> m_activePartProvider;

    ChatTranscript *m_transcript = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_scopeDocumentButton = nullptr;
    QPushButton *m_scopePageButton = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QToolButton *m_thinkingButton = nullptr;
    QToolButton *m_webButton = nullptr;
    WebSearchClient *m_webSearch = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_documentLabel = nullptr;
    QFrame *m_setupBanner = nullptr;
    bool m_pageOnly = false;
    bool m_stoppedByUser = false;
    bool m_applyingTheme = false;
    int m_nextSerial = 0;
    int m_pendingAssistantSerial = -1;
    QList<int> m_historySerials;

    QJsonArray m_history;

    QVector<IndexedChunk> m_index;
    QString m_indexDocumentId;
    QString m_indexModel;
    QString m_indexError;
    int m_indexBatchStart = 0;
    bool m_indexing = false;
    bool m_indexReady = false;
    bool m_indexErrorShown = false;

    QString m_pendingQuestion;
    int m_pendingPage = 0;
    QString m_pendingWebQuestion;
    int m_pendingWebPage = 0;
    QString m_webContext;

    QPointer<QObject> m_cachedPart;
    QString m_cachedDocumentId;
    int m_cachedPageCount = 0;
    QString m_documentTitle;
    QVector<QString> m_pages;
};

#endif
