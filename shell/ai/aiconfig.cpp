/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aiconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <KConfigGroup>
#include <KSharedConfig>

namespace
{
QString translate(const char *text)
{
    return QCoreApplication::translate("AiConfig", text);
}

bool isLikelyKey(const QString &value)
{
    if (value.size() < 16 || value.size() > 512) {
        return false;
    }
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9_\\-\\.]+$"));
    return allowed.match(value).hasMatch();
}

QString findKeyInJson(const QJsonObject &object)
{
    static const QStringList knownNames = {
        QStringLiteral("api_key"),
        QStringLiteral("apikey"),
        QStringLiteral("api-key"),
        QStringLiteral("deepseek_api_key"),
        QStringLiteral("deepseekapikey"),
        QStringLiteral("openai_api_key"),
        QStringLiteral("openai"),
        QStringLiteral("deepseek"),
        QStringLiteral("token"),
        QStringLiteral("key"),
    };

    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!it.value().isString()) {
            continue;
        }
        const QString name = it.key().toLower();
        const QString value = it.value().toString().trimmed();
        if (knownNames.contains(name) && isLikelyKey(value)) {
            return value;
        }
    }

    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.value().isObject()) {
            const QString value = findKeyInJson(it.value().toObject());
            if (!value.isEmpty()) {
                return value;
            }
        } else if (it.value().isArray()) {
            const QJsonArray array = it.value().toArray();
            for (const QJsonValue entry : array) {
                if (entry.isObject()) {
                    const QString value = findKeyInJson(entry.toObject());
                    if (!value.isEmpty()) {
                        return value;
                    }
                }
            }
        }
    }
    return QString();
}
}

AiConfig AiConfig::load()
{
    AiConfig config;
    const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("okularairc")), QStringLiteral("AI"));
    config.endpoint = group.readEntry(QStringLiteral("Endpoint"), config.endpoint);
    config.model = group.readEntry(QStringLiteral("Model"), config.model);
    config.apiKeyFile = group.readEntry(QStringLiteral("ApiKeyFile"), config.apiKeyFile);
    config.apiKey = group.readEntry(QStringLiteral("ApiKey"), config.apiKey);
    config.saveKey = !config.apiKey.isEmpty();
    config.maxContextChars = qBound(4000, group.readEntry(QStringLiteral("MaxContextChars"), config.maxContextChars), 400000);
    config.embeddingBackend = group.readEntry(QStringLiteral("EmbeddingBackend"), config.embeddingBackend);
    if (config.embeddingBackend != QStringLiteral("ollama") && config.embeddingBackend != QStringLiteral("openai")
        && config.embeddingBackend != QStringLiteral("none")) {
        config.embeddingBackend = QStringLiteral("ollama");
    }
    config.embeddingEndpoint = group.readEntry(QStringLiteral("EmbeddingEndpoint"), config.embeddingEndpoint);
    config.embeddingModel = group.readEntry(QStringLiteral("EmbeddingModel"), config.embeddingModel);
    config.showThinking = group.readEntry(QStringLiteral("ShowThinking"), config.showThinking);
    config.webSearchEnabled = group.readEntry(QStringLiteral("WebSearchEnabled"), config.webSearchEnabled);
    return config;
}

void AiConfig::save() const
{
    const KSharedConfigPtr sharedConfig = KSharedConfig::openConfig(QStringLiteral("okularairc"));
    KConfigGroup group(sharedConfig, QStringLiteral("AI"));
    group.writeEntry(QStringLiteral("Endpoint"), endpoint);
    group.writeEntry(QStringLiteral("Model"), model);
    group.writeEntry(QStringLiteral("ApiKeyFile"), apiKeyFile);
    group.writeEntry(QStringLiteral("MaxContextChars"), maxContextChars);
    group.writeEntry(QStringLiteral("EmbeddingBackend"), embeddingBackend);
    group.writeEntry(QStringLiteral("EmbeddingEndpoint"), embeddingEndpoint);
    group.writeEntry(QStringLiteral("EmbeddingModel"), embeddingModel);
    group.writeEntry(QStringLiteral("ShowThinking"), showThinking);
    group.writeEntry(QStringLiteral("WebSearchEnabled"), webSearchEnabled);
    if (saveKey && !apiKey.isEmpty()) {
        group.writeEntry(QStringLiteral("ApiKey"), apiKey);
    } else {
        group.deleteEntry(QStringLiteral("ApiKey"));
    }
    sharedConfig->sync();
}

QString AiConfig::findDefaultKeyFile()
{
    QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktop.isEmpty()) {
        desktop = QDir::homePath() + QStringLiteral("/Desktop");
    }

    const QStringList searchDirs = {desktop, QDir::homePath()};
    const QStringList knownNames = {
        QStringLiteral("api kes"),
        QStringLiteral("api keys"),
        QStringLiteral("api key"),
        QStringLiteral("api_keys"),
        QStringLiteral("api_keys.txt"),
        QStringLiteral("api_keys.json"),
        QStringLiteral("api-keys.txt"),
        QStringLiteral("apikeys.txt"),
        QStringLiteral("api.key"),
    };

    for (const QString &dirPath : searchDirs) {
        const QDir dir(dirPath);
        const QStringList files = dir.entryList(QDir::Files | QDir::Readable);
        if (files.isEmpty()) {
            continue;
        }

        for (const QString &candidate : knownNames) {
            for (const QString &file : files) {
                if (file.trimmed().compare(candidate, Qt::CaseInsensitive) == 0) {
                    return dir.absoluteFilePath(file);
                }
            }
        }

        for (const QString &file : files) {
            const QString lower = file.trimmed().toLower();
            if (lower.contains(QStringLiteral("api")) && (lower.contains(QStringLiteral("key")) || lower.contains(QStringLiteral("kes")))) {
                return dir.absoluteFilePath(file);
            }
        }
    }

    return QString();
}

QString AiConfig::readApiKeyFromFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = translate("Could not open the API key file.");
        }
        return QString();
    }

    const QByteArray data = file.read(512 * 1024);
    file.close();
    const QString content = QString::fromUtf8(data).trimmed();
    if (content.isEmpty()) {
        if (error) {
            *error = translate("The API key file is empty.");
        }
        return QString();
    }

    // JSON files such as {"deepseek_api_key": "sk-..."}
    {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(content.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            const QString value = findKeyInJson(document.object());
            if (!value.isEmpty()) {
                return value;
            }
        }
    }

    // dotenv / shell style: DEEPSEEK_API_KEY=... or "api key: ..."
    static const QRegularExpression assignment(
        QStringLiteral(R"((?:OKULAR_AI|DEEPSEEK|OPENAI)?[_\s-]*API[_\s-]*KEY\s*[:=]\s*["']?([A-Za-z0-9_\-\.]{16,}))"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = assignment.match(content);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    // Any sk- style token.
    static const QRegularExpression skToken(QStringLiteral(R"(\b(sk-[A-Za-z0-9_\-]{16,})\b)"));
    match = skToken.match(content);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    // Line based "key = value" / "key: value".
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        int separator = -1;
        for (const QChar candidate : {QLatin1Char('='), QLatin1Char(':')}) {
            const int index = line.indexOf(candidate);
            if (index > 0 && (separator < 0 || index < separator)) {
                separator = index;
            }
        }
        if (separator < 0) {
            continue;
        }
        QString value = line.mid(separator + 1).trimmed();
        if ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))) {
            value = value.mid(1, value.size() - 2).trimmed();
        }
        if (isLikelyKey(value)) {
            return value;
        }
    }

    // A file containing nothing but the key.
    if (isLikelyKey(content)) {
        return content;
    }

    if (error) {
        *error = translate("No API key could be found in the key file.");
    }
    return QString();
}

bool AiConfig::isLocalEndpoint(const QString &endpoint)
{
    QUrl url(endpoint.trimmed());
    if (url.scheme().isEmpty()) {
        url = QUrl(QStringLiteral("http://") + endpoint.trimmed());
    }
    const QString host = url.host().toLower();
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1")
        || host.endsWith(QLatin1String(".localhost"));
}

QString AiConfig::resolveApiKey(QString *source, QString *error) const
{
    if (!apiKey.trimmed().isEmpty()) {
        if (source) {
            *source = translate("settings");
        }
        return apiKey.trimmed();
    }

    for (const char *name : {"OKULAR_AI_API_KEY", "DEEPSEEK_API_KEY", "OPENAI_API_KEY"}) {
        const QString value = qEnvironmentVariable(name).trimmed();
        if (!value.isEmpty()) {
            if (source) {
                *source = translate("environment");
            }
            return value;
        }
    }

    QString path = apiKeyFile.trimmed();
    if (!path.isEmpty()) {
        if (!QFileInfo::exists(path)) {
            if (error) {
                *error = translate("The configured API key file does not exist.");
            }
            return QString();
        }
    } else {
        path = findDefaultKeyFile();
    }

    if (path.isEmpty()) {
        if (error) {
            *error = translate("No API key is configured. Add one in the AI settings or put a key file on your Desktop.");
        }
        return QString();
    }

    const QString key = readApiKeyFromFile(path, error);
    if (!key.isEmpty() && source) {
        *source = QFileInfo(path).fileName().trimmed();
    }
    return key;
}
