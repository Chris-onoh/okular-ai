/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "chattranscript.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextDocument>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <utility>

#include <KLocalizedString>

namespace
{
QFont scaledFont(const QFont &base, int pointDelta, QFont::Weight weight = QFont::Normal)
{
    QFont font = base;
    font.setPointSize(qMax(7, base.pointSize() + pointDelta));
    font.setWeight(weight);
    return font;
}
}

class ChatBubble : public QWidget
{
public:
    enum Role {
        User,
        Assistant,
    };

    ChatBubble(Role role, const QString &text, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_role(role)
    {
        setObjectName(QStringLiteral("chatBubble"));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

        auto *outer = new QHBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        if (role == User) {
            outer->addStretch(1);
        }

        m_bubble = new QFrame(this);
        m_bubble->setObjectName(role == User ? QStringLiteral("chatBubbleUser") : QStringLiteral("chatBubbleAssistant"));
        m_bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        outer->addWidget(m_bubble);
        if (role != User) {
            outer->addStretch(1);
        }

        auto *bubbleLayout = new QVBoxLayout(m_bubble);
        bubbleLayout->setContentsMargins(14, 10, 14, 12);
        bubbleLayout->setSpacing(6);

        auto *meta = new QHBoxLayout;
        meta->setSpacing(8);

        auto *name = new QLabel(role == User ? i18n("You") : i18n("Okular"), m_bubble);
        name->setObjectName(QStringLiteral("bubbleMeta"));
        name->setFont(scaledFont(font(), -1, QFont::DemiBold));
        meta->addWidget(name);
        meta->addStretch(1);

        m_time = new QLabel(QTime::currentTime().toString(QStringLiteral("HH:mm")), m_bubble);
        m_time->setObjectName(QStringLiteral("bubbleMeta"));
        m_time->setFont(scaledFont(font(), -2));
        meta->addWidget(m_time);

        bubbleLayout->addLayout(meta);

        if (role == Assistant) {
            m_thinkingBox = new QFrame(m_bubble);
            m_thinkingBox->setObjectName(QStringLiteral("thinkingBox"));
            auto *thinkingLayout = new QVBoxLayout(m_thinkingBox);
            thinkingLayout->setContentsMargins(10, 6, 10, 6);
            thinkingLayout->setSpacing(4);

            m_thinkingToggle = new QToolButton(m_thinkingBox);
            m_thinkingToggle->setObjectName(QStringLiteral("thinkingToggle"));
            m_thinkingToggle->setCheckable(true);
            m_thinkingToggle->setAutoRaise(true);
            m_thinkingToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            m_thinkingToggle->setArrowType(Qt::RightArrow);
            m_thinkingToggle->setText(i18n("Thinking"));
            m_thinkingToggle->setCursor(Qt::PointingHandCursor);
            m_thinkingToggle->setAccessibleName(i18n("Show or hide the model's thinking"));
            thinkingLayout->addWidget(m_thinkingToggle);

            m_thinkingContent = new QLabel(m_thinkingBox);
            m_thinkingContent->setObjectName(QStringLiteral("thinkingContent"));
            m_thinkingContent->setWordWrap(true);
            m_thinkingContent->setTextFormat(Qt::PlainText);
            m_thinkingContent->setTextInteractionFlags(Qt::TextSelectableByMouse);
            m_thinkingContent->setMaximumHeight(0);
            thinkingLayout->addWidget(m_thinkingContent);

            bubbleLayout->addWidget(m_thinkingBox);
            m_thinkingBox->setVisible(false);

            connect(m_thinkingToggle, &QToolButton::toggled, this, [this](bool checked) {
                animateThinking(checked);
            });

            m_thinkingTimer = new QTimer(this);
            m_thinkingTimer->setSingleShot(true);
            m_thinkingTimer->setInterval(100);
            connect(m_thinkingTimer, &QTimer::timeout, this, [this]() {
                m_thinkingContent->setText(m_thinkingText);
                if (m_thinkingToggle && m_thinkingToggle->isChecked() && !m_thinkingAnimation) {
                    m_thinkingContent->setMaximumHeight(qMin(260, m_thinkingContent->sizeHint().height()));
                }
                if (m_contentChangedHandler) {
                    m_contentChangedHandler();
                }
            });
        }

        m_content = new QLabel(m_bubble);
        m_content->setObjectName(QStringLiteral("bubbleContent"));
        m_content->setWordWrap(true);
        m_content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        m_content->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
        m_content->setFocusPolicy(Qt::StrongFocus);
        m_content->setOpenExternalLinks(false);
        m_content->setTextFormat(Qt::RichText);
        bubbleLayout->addWidget(m_content);

        auto *actions = new QHBoxLayout;
        actions->setSpacing(2);
        actions->addStretch(1);

        auto makeAction = [this, actions](const QString &themeIcon, const QString &fallbackIcon, const QString &tooltip, std::function<void()> handler) {
            auto *button = new QToolButton(m_bubble);
            button->setObjectName(QStringLiteral("messageAction"));
            button->setAutoRaise(true);
            button->setIcon(QIcon::fromTheme(themeIcon, QIcon::fromTheme(fallbackIcon)));
            button->setIconSize(QSize(14, 14));
            button->setFixedSize(26, 24);
            button->setToolTip(tooltip);
            button->setAccessibleName(tooltip);
            button->setCursor(Qt::PointingHandCursor);
            connect(button, &QToolButton::clicked, this, [this, handler]() {
                if (handler) {
                    handler();
                }
            });
            actions->addWidget(button);
            return button;
        };

        if (role == User) {
            makeAction(QStringLiteral("edit-rename"), QStringLiteral("document-edit"), i18n("Edit message"), [this]() {
                if (m_editHandler) {
                    m_editHandler(m_historySerial, m_plainText);
                }
            });
        }
        makeAction(QStringLiteral("edit-copy"), QString(), i18n("Copy message"), [this]() {
            QGuiApplication::clipboard()->setText(m_role == User ? m_plainText : m_markdown);
        });

        bubbleLayout->addLayout(actions);

        if (role == User) {
            m_plainText = text;
            m_content->setTextFormat(Qt::PlainText);
            m_content->setText(text);
        } else {
            m_content->setText(QStringLiteral("<i>") + i18n("Thinking…") + QStringLiteral("</i>"));
        }

        connect(m_content, &QLabel::linkActivated, this, [this](const QString &link) {
            if (link.startsWith(QLatin1String("okular-page:"))) {
                bool ok = false;
                const int page = link.mid(QStringLiteral("okular-page:").size()).toInt(&ok);
                if (ok && page > 0 && m_pageLinkHandler) {
                    m_pageLinkHandler(page);
                }
                return;
            }
            QDesktopServices::openUrl(QUrl(link));
        });

        m_renderTimer = new QTimer(this);
        m_renderTimer->setSingleShot(true);
        m_renderTimer->setInterval(80);
        connect(m_renderTimer, &QTimer::timeout, this, &ChatBubble::render);
    }

    void setMaximumBubbleWidth(int width)
    {
        m_bubble->setMaximumWidth(qMax(220, width));
    }

    void setHistorySerial(int serial)
    {
        m_historySerial = serial;
    }

    int historySerial() const
    {
        return m_historySerial;
    }

    void setPageLinkHandler(std::function<void(int)> handler)
    {
        m_pageLinkHandler = std::move(handler);
    }

    void setContentChangedHandler(std::function<void()> handler)
    {
        m_contentChangedHandler = std::move(handler);
    }

    void setEditHandler(std::function<void(int, const QString &)> handler)
    {
        m_editHandler = std::move(handler);
    }

    void setMarkdown(const QString &markdown)
    {
        m_markdown = markdown;
        m_hasContent = !markdown.trimmed().isEmpty();
        render();
    }

    void appendChunk(const QString &chunk)
    {
        finishThinking();
        m_markdown += chunk;
        m_hasContent = true;
        if (!m_renderTimer->isActive()) {
            m_renderTimer->start();
        }
    }

    void finishStreaming()
    {
        m_renderTimer->stop();
        finishThinking();
        render();
        if (!m_hasContent) {
            m_content->setText(QStringLiteral("<i>") + i18n("No response.") + QStringLiteral("</i>"));
        }
    }

    void beginThinking()
    {
        if (!m_thinkingBox) {
            return;
        }
        m_thinkingBox->setVisible(true);
        m_thinkingActive = true;
        if (!m_thinkingToggle->isChecked()) {
            m_thinkingToggle->setChecked(true);
        } else {
            animateThinking(true);
        }
    }

    void appendThinking(const QString &chunk)
    {
        if (!m_thinkingBox) {
            return;
        }
        if (!m_thinkingActive) {
            beginThinking();
        }
        m_thinkingText += chunk;
        if (m_thinkingText.size() > 6000) {
            m_thinkingText = m_thinkingText.left(6000) + QStringLiteral("…");
        }
        if (!m_thinkingTimer->isActive()) {
            m_thinkingTimer->start();
        }
    }

    void finishThinking()
    {
        if (!m_thinkingActive) {
            return;
        }
        m_thinkingActive = false;
        if (m_thinkingTimer) {
            m_thinkingTimer->stop();
        }
        if (m_thinkingContent) {
            m_thinkingContent->setText(m_thinkingText);
        }
        if (m_thinkingToggle) {
            m_thinkingToggle->setText(i18n("Show thinking"));
            m_thinkingToggle->setChecked(false);
        }
    }

    void animateThinking(bool expanded)
    {
        if (!m_thinkingContent) {
            return;
        }
        const int target = expanded ? qMin(260, qMax(24, m_thinkingContent->sizeHint().height())) : 0;
        auto *animation = new QPropertyAnimation(m_thinkingContent, "maximumHeight", this);
        animation->setDuration(160);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        animation->setStartValue(m_thinkingContent->maximumHeight());
        animation->setEndValue(target);
        if (m_thinkingAnimation) {
            m_thinkingAnimation->stop();
        }
        m_thinkingAnimation = animation;
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            m_thinkingAnimation = nullptr;
        });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
        if (m_thinkingToggle) {
            m_thinkingToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        }
    }

    void animateIn()
    {
        auto *effect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(effect);
        auto *animation = new QPropertyAnimation(effect, "opacity", this);
        animation->setDuration(180);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            setGraphicsEffect(nullptr);
        });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }

private:
    void render()
    {
        if (m_role == User || !m_hasContent) {
            return;
        }
        m_content->setText(renderMarkdown(m_markdown));
        if (m_contentChangedHandler) {
            m_contentChangedHandler();
        }
    }

    static QString renderMarkdown(const QString &markdown)
    {
        QString text = markdown;
        static const QRegularExpression citation(QStringLiteral("\\[(?:page|p\\.?)\\s*(\\d+)\\]"), QRegularExpression::CaseInsensitiveOption);
        text.replace(citation, QStringLiteral("[page \\1](okular-page:\\1)"));

        QTextDocument document;
        document.setDocumentMargin(0);
        document.setDefaultFont(QGuiApplication::font());
        document.setMarkdown(text, QTextDocument::MarkdownDialectGitHub);

        QString html = document.toHtml();

        // QTextDocument embeds the default (often black) colors in its HTML.
        // Rewrite them so the answer follows the current palette.
        const QString textColor = QGuiApplication::palette().color(QPalette::WindowText).name();
        html.replace(QRegularExpression(QStringLiteral("color:\\s*#[0-9a-fA-F]{6}")), QStringLiteral("color:%1").arg(textColor));
        html.replace(QRegularExpression(QStringLiteral("background-color:\\s*#[0-9a-fA-F]{6}")), QStringLiteral("background-color:transparent"));

        const QString linkColor = QGuiApplication::palette().color(QPalette::Link).name();
        html.replace(QStringLiteral("<a href=\""), QStringLiteral("<a style=\"color:%1;\" href=\"").arg(linkColor));
        return html;
    }

    Role m_role;
    QFrame *m_bubble = nullptr;
    QLabel *m_content = nullptr;
    QLabel *m_time = nullptr;
    QTimer *m_renderTimer = nullptr;
    QString m_plainText;
    QString m_markdown;
    std::function<void(int)> m_pageLinkHandler;
    std::function<void()> m_contentChangedHandler;
    std::function<void(int, const QString &)> m_editHandler;
    int m_historySerial = -1;
    bool m_hasContent = false;
    QWidget *m_thinkingBox = nullptr;
    QToolButton *m_thinkingToggle = nullptr;
    QLabel *m_thinkingContent = nullptr;
    QTimer *m_thinkingTimer = nullptr;
    QPropertyAnimation *m_thinkingAnimation = nullptr;
    QString m_thinkingText;
    bool m_thinkingActive = false;
};

ChatTranscript::ChatTranscript(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("aiTranscript"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setAutoFillBackground(false);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        m_stayAtBottom = value >= verticalScrollBar()->maximum() - 24;
    });

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("aiTranscriptContent"));
    m_content->setAutoFillBackground(false);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(4, 4, 4, 4);
    m_contentLayout->setSpacing(6);

    m_emptyState = new QWidget(m_content);
    m_emptyState->setObjectName(QStringLiteral("aiEmptyState"));
    m_emptyState->setAutoFillBackground(false);
    m_emptyState->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(10, 12, 10, 12);
    emptyLayout->setSpacing(8);
    emptyLayout->addStretch(1);

    auto *icon = new QLabel(m_emptyState);
    icon->setAlignment(Qt::AlignCenter);
    QIcon themeIcon = QIcon::fromTheme(QStringLiteral("assistant"), QIcon::fromTheme(QStringLiteral("applications-science")));
    icon->setPixmap(themeIcon.pixmap(28, 28));
    emptyLayout->addWidget(icon);

    auto *title = new QLabel(i18n("How can I help with this document?"), m_emptyState);
    title->setObjectName(QStringLiteral("emptyTitle"));
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);
    title->setFont(scaledFont(font(), 2, QFont::DemiBold));
    emptyLayout->addWidget(title);

    auto *subtitle = new QLabel(i18n("Answers are grounded in the open document. Click a page citation to jump there."), m_emptyState);
    subtitle->setObjectName(QStringLiteral("emptySubtitle"));
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    subtitle->setFont(scaledFont(font(), -1));
    emptyLayout->addWidget(subtitle);

    auto *suggestions = new QGridLayout;
    suggestions->setHorizontalSpacing(8);
    suggestions->setVerticalSpacing(8);
    suggestions->setContentsMargins(4, 10, 4, 0);

    const QList<QPair<QString, QString>> prompts = {
        {i18n("Summarize"), i18n("Summarize this document. Cover the main topics, structure and conclusions, and cite pages.")},
        {i18n("Explain this page"), i18n("Explain the content of the current page in clear, simple terms.")},
        {i18n("Key findings"), i18n("What are the key findings and most important facts in this document?")},
        {i18n("Risks & actions"), i18n("List the risks, open questions and action items mentioned in this document.")},
    };

    int row = 0;
    int column = 0;
    for (const auto &prompt : prompts) {
        auto *button = new QPushButton(prompt.first, m_emptyState);
        button->setObjectName(QStringLiteral("suggestionButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(42);
        button->setAccessibleName(prompt.first);
        connect(button, &QPushButton::clicked, this, [this, prompt]() {
            Q_EMIT suggestionActivated(prompt.second);
        });
        suggestions->addWidget(button, row, column);
        ++column;
        if (column == 2) {
            column = 0;
            ++row;
        }
    }
    emptyLayout->addLayout(suggestions);
    emptyLayout->addStretch(1);

    m_messages = new QWidget(m_content);
    m_messages->setObjectName(QStringLiteral("aiMessages"));
    m_messages->setAutoFillBackground(false);
    m_messagesLayout = new QVBoxLayout(m_messages);
    m_messagesLayout->setContentsMargins(0, 0, 0, 0);
    m_messagesLayout->setSpacing(12);
    m_messagesLayout->addStretch(1);

    m_contentLayout->addWidget(m_emptyState);
    m_contentLayout->addWidget(m_messages);

    setWidget(m_content);
    showEmptyState(true);
}

void ChatTranscript::clear()
{
    m_currentAssistant = nullptr;
    m_messageCount = 0;

    while (m_messagesLayout->count() > 1) {
        QLayoutItem *item = m_messagesLayout->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    showEmptyState(true);
    scrollToBottom(true);
}

bool ChatTranscript::isEmpty() const
{
    return m_messageCount == 0;
}

void ChatTranscript::showEmptyState(bool show)
{
    m_emptyState->setVisible(show);
    m_messages->setVisible(!show);
    if (show) {
        m_contentLayout->setStretch(0, 1);
        m_contentLayout->setStretch(1, 0);
    } else {
        m_contentLayout->setStretch(0, 0);
        m_contentLayout->setStretch(1, 1);
    }
}

void ChatTranscript::ensureMessagesVisible()
{
    if (m_messageCount == 0) {
        showEmptyState(false);
    }
    ++m_messageCount;
}

void ChatTranscript::addSystemMessage(const QString &text)
{
    auto *label = new QLabel(text, m_messages);
    label->setObjectName(QStringLiteral("chatSystem"));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setFont(scaledFont(font(), -1));
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, label);

    ensureMessagesVisible();
    scrollToBottom(true);
}

void ChatTranscript::addUserMessage(const QString &text, int historySerial)
{
    auto *bubble = new ChatBubble(ChatBubble::User, text, m_messages);
    bubble->setHistorySerial(historySerial);
    bubble->animateIn();
    bubble->setEditHandler([this](int serial, const QString &editedText) {
        Q_EMIT editRequested(serial, editedText);
    });
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, bubble);
    ensureMessagesVisible();
    updateBubbleWidths();
    scrollToBottom(true);
}

void ChatTranscript::addAssistantMessage(const QString &markdown, int historySerial)
{
    auto *bubble = new ChatBubble(ChatBubble::Assistant, QString(), m_messages);
    bubble->setHistorySerial(historySerial);
    bubble->setPageLinkHandler([this](int page) { Q_EMIT pageLinkActivated(page); });
    bubble->setContentChangedHandler([this]() { scrollToBottom(false); });
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, bubble);
    bubble->setMarkdown(markdown);
    bubble->finishStreaming();
    ensureMessagesVisible();
    updateBubbleWidths();
    scrollToBottom(true);
}

void ChatTranscript::beginAssistantMessage(int historySerial)
{
    if (m_currentAssistant) {
        endAssistantMessage();
    }

    m_currentAssistant = new ChatBubble(ChatBubble::Assistant, QString(), m_messages);
    m_currentAssistant->setHistorySerial(historySerial);
    m_currentAssistant->setPageLinkHandler([this](int page) { Q_EMIT pageLinkActivated(page); });
    m_currentAssistant->setContentChangedHandler([this]() { scrollToBottom(false); });
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, m_currentAssistant);
    m_currentAssistant->animateIn();

    ensureMessagesVisible();
    updateBubbleWidths();
    scrollToBottom(true);
}

void ChatTranscript::appendAssistantChunk(const QString &chunk)
{
    if (!m_currentAssistant) {
        beginAssistantMessage();
    }
    m_currentAssistant->appendChunk(chunk);
}

void ChatTranscript::appendAssistantReasoning(const QString &chunk)
{
    if (!m_currentAssistant) {
        beginAssistantMessage();
    }
    m_currentAssistant->appendThinking(chunk);
}

void ChatTranscript::finishAssistantReasoning()
{
    if (m_currentAssistant) {
        m_currentAssistant->finishThinking();
    }
}

void ChatTranscript::endAssistantMessage()
{
    if (!m_currentAssistant) {
        return;
    }
    m_currentAssistant->finishStreaming();
    m_currentAssistant = nullptr;
    scrollToBottom(false);
}

void ChatTranscript::removeMessagesFromSerial(int historySerial)
{
    int first = -1;
    const int lastItem = m_messagesLayout->count() - 1;
    for (int index = 0; index < lastItem; ++index) {
        QLayoutItem *item = m_messagesLayout->itemAt(index);
        if (auto *bubble = dynamic_cast<ChatBubble *>(item->widget())) {
            if (bubble->historySerial() >= 0 && bubble->historySerial() >= historySerial) {
                first = index;
                break;
            }
        }
    }
    if (first < 0) {
        return;
    }

    while (m_messagesLayout->count() - 1 > first) {
        QLayoutItem *item = m_messagesLayout->takeAt(first);
        if (QWidget *widget = item->widget()) {
            if (widget == m_currentAssistant) {
                m_currentAssistant = nullptr;
            }
            widget->deleteLater();
        }
        delete item;
    }

    m_messageCount = first;
    if (m_messageCount == 0) {
        showEmptyState(true);
    }
    scrollToBottom(true);
}

void ChatTranscript::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    updateBubbleWidths();
}

void ChatTranscript::updateBubbleWidths()
{
    const int width = qMax(220, static_cast<int>(viewport()->width() * 0.9));
    const QObjectList children = m_messages->children();
    for (QObject *child : children) {
        if (auto *bubble = dynamic_cast<ChatBubble *>(child)) {
            bubble->setMaximumBubbleWidth(width);
        }
    }
}

void ChatTranscript::scrollToBottom(bool force)
{
    if (!force && !m_stayAtBottom) {
        return;
    }
    m_stayAtBottom = true;
    QTimer::singleShot(0, this, [this, force]() {
        if (force || m_stayAtBottom) {
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        }
    });
}
