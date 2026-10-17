/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_MOBILE_AICHATMODEL_H
#define OKULAR_MOBILE_AICHATMODEL_H

#include <QAbstractListModel>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

/**
 * Chat transcript model for the mobile/Kirigami UI.
 *
 * Streaming updates are batched at roughly 30 frames per second so a fast
 * server cannot force a QML layout update for every token.
 */
class AiChatModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        AuthorRole = Qt::UserRole + 1,
        TextRole,
        ReasoningRole,
        StreamingRole,
        SerialRole,
    };

    explicit AiChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int addMessage(const QString &author, const QString &text, int serial, bool streaming = false);
    void appendText(int row, const QString &chunk);
    void appendReasoning(int row, const QString &chunk);
    void finishMessage(int row);
    void clearMessages();
    void removeFromSerial(int serial);

    QString messageText(int row) const;
    QString messageAuthor(int row) const;
    int messageSerial(int row) const;
    bool isStreaming(int row) const;

private:
    struct Message {
        QString author;
        QString text;
        QString reasoning;
        QString display;
        int serial = -1;
        bool streaming = false;
        bool displayValid = false;
    };

    void markDirty(int row);
    void flushDirty();
    static QString formatAssistant(const QString &text);

    QVector<Message> m_messages;
    QSet<int> m_dirty;
    QTimer m_updateTimer;
};

#endif
