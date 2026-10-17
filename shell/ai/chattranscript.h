/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_AI_CHATTRANSCRIPT_H
#define OKULAR_AI_CHATTRANSCRIPT_H

#include <QScrollArea>
#include <QString>

class QLabel;
class QVBoxLayout;
class QWidget;
class ChatBubble;

/**
 * Scrollable conversation view.
 *
 * Assistant content is rendered as Markdown and laid out like a chat
 * assistant: flat text with a copy action, while user messages are accent
 * bubbles with edit and copy actions. Citations such as [page 3] are
 * clickable. Auto-scrolls while the user is at the bottom.
 */
class ChatTranscript : public QScrollArea
{
    Q_OBJECT

public:
    explicit ChatTranscript(QWidget *parent = nullptr);

    void clear();
    bool isEmpty() const;

    void addSystemMessage(const QString &text);
    void addUserMessage(const QString &text, int historySerial = -1);
    void addAssistantMessage(const QString &markdown, int historySerial = -1);

    void beginAssistantMessage(int historySerial = -1);
    void appendAssistantChunk(const QString &chunk);
    void appendAssistantReasoning(const QString &chunk);
    void finishAssistantReasoning();
    void endAssistantMessage();

    /**
     * Removes the message with the given history serial and everything after
     * it. Used when the user edits a message.
     */
    void removeMessagesFromSerial(int historySerial);

Q_SIGNALS:
    void pageLinkActivated(int page);
    void suggestionActivated(const QString &prompt);
    void editRequested(int historySerial, const QString &text);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void showEmptyState(bool show);
    void scrollToBottom(bool force);
    void updateBubbleWidths();
    void ensureMessagesVisible();

    QWidget *m_content = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QWidget *m_emptyState = nullptr;
    QWidget *m_messages = nullptr;
    QVBoxLayout *m_messagesLayout = nullptr;
    ChatBubble *m_currentAssistant = nullptr;
    int m_messageCount = 0;
    bool m_stayAtBottom = true;
};

#endif
