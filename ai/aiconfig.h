/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_AI_AICONFIG_H
#define OKULAR_AI_AICONFIG_H

#include <QString>

/**
 * Configuration and API key discovery for the AI assistant.
 *
 * Secrets are never written to the source tree. The key is resolved at
 * runtime from (in order):
 *   1. the key entered in the settings dialog for this session
 *   2. the OKULAR_AI_API_KEY / DEEPSEEK_API_KEY / OPENAI_API_KEY env vars
 *   3. an explicitly configured key file
 *   4. a key file auto-discovered on the Desktop (e.g. "api kes")
 */
struct AiConfig {
    QString provider = QStringLiteral("deepseek"); // "deepseek", "ollama", "llamacpp", "custom"
    QString endpoint = QStringLiteral("https://api.deepseek.com/chat/completions");
    QString model = QStringLiteral("deepseek-flash");
    QString apiKeyFile;
    QString apiKey;
    bool saveKey = false;
    int maxContextChars = 48000;

    QString embeddingBackend = QStringLiteral("ollama"); // "ollama", "openai", "none"
    QString embeddingEndpoint = QStringLiteral("http://127.0.0.1:11434/api/embed");
    QString embeddingModel = QStringLiteral("all-minilm");

    bool showThinking = true;
    bool webSearchEnabled = false;

    static AiConfig load();
    void save() const;

    /**
     * Returns the key file that would be used if none is configured
     * explicitly, or an empty string.
     */
    static QString findDefaultKeyFile();

    /**
     * True for localhost endpoints, which usually need no API key.
     */
    static bool isLocalEndpoint(const QString &endpoint);

    /**
     * Reads an API key from a text/JSON/dotenv file. Never returns the file
     * contents in the error message.
     */
    static QString readApiKeyFromFile(const QString &path, QString *error = nullptr);

    /**
     * Resolves the API key and reports a human readable source (file name,
     * "environment", "settings"). The returned key must not be displayed.
     */
    QString resolveApiKey(QString *source = nullptr, QString *error = nullptr) const;
};

#endif
