/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_MOBILE_AIBACKEND_H
#define OKULAR_MOBILE_AIBACKEND_H

#include "aichatmodel.h"
#include "aiconfig.h"

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <qqmlintegration.h>

class DocumentItem;
class LlmClient;
class ModelCatalog;
class WebSearchClient;
struct WebSearchResult;

/**
 * Mobile/Kirigami AI controller.
 *
 * It reuses the shared AI core (configuration, streaming, model discovery and
 * web search) and adapts it to the Kirigami document item. Document text is
 * extracted lazily and cached, retrieval is keyword based and bounded so a
 * phone never has to build a large vector index.
 */
class AiBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(AiChatModel *chatModel READ chatModel CONSTANT)
    Q_PROPERTY(QObject *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool thinkingEnabled READ thinkingEnabled WRITE setThinkingEnabled NOTIFY thinkingEnabledChanged)
    Q_PROPERTY(bool thinkingSupported READ thinkingSupported NOTIFY thinkingCapabilityChanged)
    Q_PROPERTY(bool thinkingSupportKnown READ thinkingSupportKnown NOTIFY thinkingCapabilityChanged)
    Q_PROPERTY(bool webSearchEnabled READ webSearchEnabled WRITE setWebSearchEnabled NOTIFY webSearchEnabledChanged)
    Q_PROPERTY(bool pageScope READ pageScope WRITE setPageScope NOTIFY pageScopeChanged)
    Q_PROPERTY(bool needsSetup READ needsSetup NOTIFY needsSetupChanged)
    Q_PROPERTY(bool keyConfigured READ keyConfigured NOTIFY keyConfiguredChanged)
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY providerChanged)
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY endpointChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QStringList availableModels READ availableModels NOTIFY availableModelsChanged)
    Q_PROPERTY(QString documentTitle READ documentTitle NOTIFY documentInfoChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentInfoChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY documentInfoChanged)
    Q_PROPERTY(bool rememberKey READ rememberKey WRITE setRememberKey NOTIFY rememberKeyChanged)
    Q_PROPERTY(int contextBudget READ contextBudget WRITE setContextBudget NOTIFY contextBudgetChanged)

public:
    explicit AiBackend(QObject *parent = nullptr);
    ~AiBackend() override;

    AiChatModel *chatModel() const;
    QObject *document() const;
    void setDocument(QObject *document);

    bool busy() const;
    QString status() const;
    bool thinkingEnabled() const;
    void setThinkingEnabled(bool enabled);
    bool thinkingSupported() const;
    bool thinkingSupportKnown() const;
    bool webSearchEnabled() const;
    void setWebSearchEnabled(bool enabled);
    bool pageScope() const;
    void setPageScope(bool pageScope);
    bool needsSetup() const;
    bool keyConfigured() const;

    QString provider() const;
    void setProvider(const QString &provider);
    QString endpoint() const;
    void setEndpoint(const QString &endpoint);
    QString model() const;
    void setModel(const QString &model);
    QStringList availableModels() const;
    QString documentTitle() const;
    int pageCount() const;
    int currentPage() const;
    bool rememberKey() const;
    void setRememberKey(bool remember);
    int contextBudget() const;
    void setContextBudget(int budget);

    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clearConversation();
    Q_INVOKABLE void goToPage(int page);
    Q_INVOKABLE void detectModels();
    Q_INVOKABLE void applyProvider(const QString &provider);
    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE QString takeMessageForEdit(int row);
    Q_INVOKABLE void refreshThinkingCapability();

Q_SIGNALS:
    void documentChanged();
    void documentInfoChanged();
    void busyChanged();
    void statusChanged();
    void thinkingEnabledChanged();
    void thinkingCapabilityChanged();
    void webSearchEnabledChanged();
    void pageScopeChanged();
    void needsSetupChanged();
    void keyConfiguredChanged();
    void providerChanged();
    void endpointChanged();
    void modelChanged();
    void availableModelsChanged();
    void rememberKeyChanged();
    void contextBudgetChanged();

private:
    void invalidateDocumentCache();
    bool ensureDocumentText(QString *error);
    QString buildContext(const QString &question, int pageNumber, QString *notice) const;
    void proceed(const QString &question, int pageNumber);
    void sendQuestion(const QString &question, const QString &context, const QString &notice);
    void onWebSearchFinished(const QVector<WebSearchResult> &results);
    void onWebSearchFailed(const QString &message);
    QString formatWebResults(const QVector<WebSearchResult> &results) const;
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void updateSetupState();
    void updateDocumentInfo();
    void trimHistory();

    AiConfig m_config;
    AiChatModel *m_chatModel = nullptr;
    LlmClient *m_client = nullptr;
    ModelCatalog *m_catalog = nullptr;
    WebSearchClient *m_webSearch = nullptr;
    QPointer<DocumentItem> m_document;

    QString m_documentId;
    QString m_documentTitle;
    int m_documentPageCount = 0;
    QVector<QString> m_pages;

    QJsonArray m_history;
    QList<int> m_historySerials;
    int m_nextSerial = 0;
    int m_assistantRow = -1;
    int m_pendingAssistantSerial = -1;

    QString m_pendingQuestion;
    int m_pendingPage = 0;
    QString m_webContext;
    bool m_stoppedByUser = false;
    bool m_busy = false;
    bool m_pageScope = false;
    QString m_status;

    QStringList m_availableModels;
    int m_thinkingSupport = -1; // 1 supported, 0 unsupported, -1 unknown
    bool m_needsSetup = false;
    bool m_keyConfigured = false;
};

#endif
