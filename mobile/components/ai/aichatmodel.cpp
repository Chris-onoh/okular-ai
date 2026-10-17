/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aichatmodel.h"

#include <QRegularExpression>

#include <algorithm>
#include <utility>

AiChatModel::AiChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(33);
    connect(&m_updateTimer, &QTimer::timeout, this, &AiChatModel::flushDirty);
}

int AiChatModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_messages.size();
}

QVariant AiChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size()) {
        return {};
    }
    const Message &message = m_messages.at(index.row());
    switch (role) {
    case AuthorRole:
        return message.author;
    case TextRole:
        if (message.author == QLatin1String("assistant") && !message.streaming && message.displayValid) {
            return message.display;
        }
        return message.text;
    case ReasoningRole:
        return message.reasoning;
    case StreamingRole:
        return message.streaming;
    case SerialRole:
        return message.serial;
    default:
        return {};
    }
}

QHash<int, QByteArray> AiChatModel::roleNames() const
{
    return {
        {AuthorRole, QByteArrayLiteral("author")},
        {TextRole, QByteArrayLiteral("text")},
        {ReasoningRole, QByteArrayLiteral("reasoning")},
        {StreamingRole, QByteArrayLiteral("streaming")},
        {SerialRole, QByteArrayLiteral("serial")},
    };
}

int AiChatModel::addMessage(const QString &author, const QString &text, int serial, bool streaming)
{
    const int row = m_messages.size();
    beginInsertRows(QModelIndex(), row, row);

    Message message;
    message.author = author;
    message.text = text;
    message.serial = serial;
    message.streaming = streaming;
    message.displayValid = !streaming;
    if (message.displayValid) {
        message.display = formatAssistant(text);
    }
    m_messages.append(message);

    endInsertRows();
    return row;
}

void AiChatModel::appendText(int row, const QString &chunk)
{
    if (row < 0 || row >= m_messages.size()) {
        return;
    }
    m_messages[row].text += chunk;
    m_messages[row].displayValid = false;
    markDirty(row);
}

void AiChatModel::appendReasoning(int row, const QString &chunk)
{
    if (row < 0 || row >= m_messages.size()) {
        return;
    }
    Message &message = m_messages[row];
    message.reasoning += chunk;
    if (message.reasoning.size() > 6000) {
        message.reasoning = message.reasoning.left(6000) + QStringLiteral("…");
    }
    markDirty(row);
}

void AiChatModel::finishMessage(int row)
{
    if (row < 0 || row >= m_messages.size()) {
        return;
    }
    Message &message = m_messages[row];
    message.streaming = false;
    message.display = formatAssistant(message.text);
    message.displayValid = true;
    m_dirty.remove(row);
    Q_EMIT dataChanged(index(row), index(row), {TextRole, StreamingRole});
}

void AiChatModel::clearMessages()
{
    if (m_messages.isEmpty()) {
        return;
    }
    beginResetModel();
    m_messages.clear();
    m_dirty.clear();
    m_updateTimer.stop();
    endResetModel();
}

void AiChatModel::removeFromSerial(int serial)
{
    int first = -1;
    for (int row = 0; row < m_messages.size(); ++row) {
        if (m_messages.at(row).serial >= serial) {
            first = row;
            break;
        }
    }
    if (first < 0) {
        return;
    }
    beginRemoveRows(QModelIndex(), first, m_messages.size() - 1);
    m_messages.resize(first);
    m_dirty.clear();
    endRemoveRows();
}

QString AiChatModel::messageText(int row) const
{
    return (row >= 0 && row < m_messages.size()) ? m_messages.at(row).text : QString();
}

QString AiChatModel::messageAuthor(int row) const
{
    return (row >= 0 && row < m_messages.size()) ? m_messages.at(row).author : QString();
}

int AiChatModel::messageSerial(int row) const
{
    return (row >= 0 && row < m_messages.size()) ? m_messages.at(row).serial : -1;
}

bool AiChatModel::isStreaming(int row) const
{
    return (row >= 0 && row < m_messages.size()) ? m_messages.at(row).streaming : false;
}

void AiChatModel::markDirty(int row)
{
    m_dirty.insert(row);
    if (!m_updateTimer.isActive()) {
        m_updateTimer.start();
    }
}

void AiChatModel::flushDirty()
{
    QList<int> rows = m_dirty.values();
    std::sort(rows.begin(), rows.end());
    for (int row : std::as_const(rows)) {
        if (row >= 0 && row < m_messages.size()) {
            Q_EMIT dataChanged(index(row), index(row), {TextRole, ReasoningRole, StreamingRole});
        }
    }
    m_dirty.clear();
}

QString AiChatModel::formatAssistant(const QString &text)
{
    static const QRegularExpression citation(QStringLiteral(R"(\[page (\d+)\])"));
    QString result = text;
    result.replace(citation, QStringLiteral("[page \\1](okular-page:\\1)"));
    return result;
}
