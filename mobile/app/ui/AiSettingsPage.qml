/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: root

    property var ai

    title: i18n("AI settings")

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.ComboBox {
                id: providerCombo
                Kirigami.FormData.label: i18n("Provider")
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: i18n("DeepSeek (cloud)"), value: "deepseek" },
                    { text: i18n("Ollama (local)"), value: "ollama" },
                    { text: i18n("llama.cpp (local)"), value: "llamacpp" },
                    { text: i18n("Custom OpenAI-compatible"), value: "custom" }
                ]
                Component.onCompleted: currentIndex = indexOfValue(root.ai.provider)
                onActivated: root.ai.applyProvider(currentValue)

                Connections {
                    target: root.ai
                    function onProviderChanged() {
                        providerCombo.currentIndex = providerCombo.indexOfValue(root.ai.provider);
                    }
                }
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Model")
                Layout.fillWidth: true

                QQC2.ComboBox {
                    id: modelCombo
                    Layout.fillWidth: true
                    editable: true
                    model: root.ai.availableModels
                    Component.onCompleted: editText = root.ai.model
                    onActivated: root.ai.model = currentText

                    Connections {
                        target: root.ai
                        function onModelChanged() {
                            modelCombo.editText = root.ai.model;
                        }
                    }
                }

                QQC2.Button {
                    text: i18n("Use")
                    onClicked: root.ai.model = modelCombo.editText
                }
            }

            QQC2.Button {
                Kirigami.FormData.label: i18n("Discover")
                text: i18n("Detect local models")
                enabled: root.ai.provider === "ollama" || root.ai.provider === "llamacpp" || root.ai.provider === "custom"
                onClicked: root.ai.detectModels()
            }

            QQC2.Switch {
                Kirigami.FormData.label: i18n("Thinking")
                text: i18n("Enable thinking (uses more tokens)")
                checked: root.ai.thinkingSupported ? root.ai.thinkingEnabled : false
                enabled: root.ai.thinkingSupported
                onToggled: root.ai.thinkingEnabled = checked
            }

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                opacity: 0.7
                text: !root.ai.thinkingSupportKnown
                    ? i18n("Thinking support could not be detected.")
                    : root.ai.thinkingSupported
                        ? i18n("This model supports thinking.")
                        : i18n("This model does not support thinking.")
            }

            QQC2.Switch {
                Kirigami.FormData.label: i18n("Web")
                text: i18n("Search the web for context")
                checked: root.ai.webSearchEnabled
                onToggled: root.ai.webSearchEnabled = checked
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Kirigami.Theme.disabledTextColor
            opacity: 0.3
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.TextField {
                id: apiKeyField
                Kirigami.FormData.label: i18n("API key")
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: root.ai.keyConfigured ? i18n("Key configured") : i18n("Paste a key")
                onEditingFinished: root.ai.setApiKey(text)
            }

            QQC2.Switch {
                Kirigami.FormData.label: i18n("Remember")
                text: i18n("Store the key on this device")
                checked: root.ai.rememberKey
                onToggled: root.ai.rememberKey = checked
            }

            QQC2.TextField {
                id: endpointField
                Kirigami.FormData.label: i18n("Endpoint")
                Layout.fillWidth: true
                Component.onCompleted: text = root.ai.endpoint
                onEditingFinished: root.ai.setEndpoint(text)

                Connections {
                    target: root.ai
                    function onEndpointChanged() {
                        endpointField.text = root.ai.endpoint;
                    }
                }
            }

            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Context budget")
                from: 4000
                to: 64000
                stepSize: 4000
                value: root.ai.contextBudget
                onValueModified: root.ai.setContextBudget(value)
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: root.ai.provider === "ollama" || root.ai.provider === "llamacpp"
            text: i18n("On Android, localhost is the phone itself. For Ollama on a computer, use its LAN address, for example http://192.168.1.10:11434/v1/chat/completions. For llama.cpp start it with --host 0.0.0.0 --port 8080 and use http://<computer-ip>:8080/v1/chat/completions.")
        }

        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            text: i18n("Changes are saved automatically. The API key is only stored when Remember is enabled.")
        }
    }
}
