/*
    SPDX-FileCopyrightText: 2026 Okular AI contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root

    property var ai
    property var document

    title: i18n("AI mode")
    padding: 0

    actions: [
        Kirigami.Action {
            text: i18n("AI settings")
            icon.name: "settings-configure"
            onTriggered: applicationWindow().pageStack.layers.push(settingsComponent)
        },
        Kirigami.Action {
            text: i18n("Clear")
            icon.name: "edit-clear-history"
            enabled: root.ai && listView.count > 0 && !root.ai.busy
            onTriggered: root.ai.clearConversation()
        }
    ]

    Component {
        id: settingsComponent
        AiSettingsPage {
            ai: root.ai
        }
    }

    TextEdit {
        id: clipboardHelper
        visible: false
        width: 0
        height: 0
    }

    function copyText(value) {
        clipboardHelper.text = value;
        clipboardHelper.selectAll();
        clipboardHelper.copy();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: root.ai && root.ai.needsSetup
            type: Kirigami.MessageType.Information
            text: i18n("No API key is configured. Add a key or use a local model in AI settings.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Switch {
                text: i18n("Thinking")
                checked: root.ai.thinkingSupported ? root.ai.thinkingEnabled : false
                enabled: root.ai.thinkingSupported
                onToggled: root.ai.thinkingEnabled = checked
            }
            QQC2.Switch {
                text: i18n("Web")
                checked: root.ai.webSearchEnabled
                onToggled: root.ai.webSearchEnabled = checked
            }
            Item { Layout.fillWidth: true }
            QQC2.ComboBox {
                model: [i18n("Document"), i18n("This page")]
                currentIndex: root.ai.pageScope ? 1 : 0
                onActivated: root.ai.pageScope = (index === 1)
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            visible: root.ai && root.ai.status.length > 0
            text: root.ai ? root.ai.status : ""
            elide: Text.ElideRight
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: listView
                anchors.fill: parent
                clip: true
            spacing: Kirigami.Units.smallSpacing
            model: root.ai ? root.ai.chatModel : null
            reuseItems: true
            cacheBuffer: 4000

            property bool stayAtBottom: true
                onCountChanged: if (stayAtBottom) Qt.callLater(positionViewAtEnd)
                onContentHeightChanged: if (stayAtBottom) positionViewAtEnd()
                onMovementEnded: stayAtBottom = atYEnd

                delegate: Item {
                id: delegateItem
                required property string author
                required property string text
                required property string reasoning
                required property bool streaming
                required property int serial

                property bool thinkingExpanded: false

                width: ListView.view.width
                implicitHeight: bubbleLayout.implicitHeight + Kirigami.Units.smallSpacing

                ColumnLayout {
                    id: bubbleLayout
                    width: parent.width
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: Kirigami.Units.smallSpacing / 2

                    Rectangle {
                        visible: delegateItem.author === "user"
                        Layout.alignment: Qt.AlignRight
                        Layout.maximumWidth: delegateItem.width * 0.82
                        Layout.preferredWidth: Math.min(userText.implicitWidth + Kirigami.Units.gridUnit, delegateItem.width * 0.82)
                        implicitHeight: userText.implicitHeight + Kirigami.Units.smallSpacing
                        radius: Kirigami.Units.gridUnit * 0.7
                        color: Kirigami.Theme.highlightColor

                        QQC2.Label {
                            id: userText
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            text: delegateItem.text
                            color: Kirigami.Theme.highlightedTextColor
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                        }
                    }

                    RowLayout {
                        visible: delegateItem.author === "user"
                        Layout.alignment: Qt.AlignRight
                        spacing: 0

                        QQC2.ToolButton {
                            icon.name: "edit-rename"
                            text: i18n("Edit")
                            display: QQC2.AbstractButton.IconOnly
                            enabled: !root.ai.busy
                            onClicked: {
                                composer.text = root.ai.takeMessageForEdit(index);
                                composer.forceActiveFocus();
                            }
                        }
                        QQC2.ToolButton {
                            icon.name: "edit-copy"
                            display: QQC2.AbstractButton.IconOnly
                            onClicked: root.copyText(delegateItem.text)
                        }
                    }

                    QQC2.Label {
                        visible: delegateItem.author === "system"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: delegateItem.text
                        wrapMode: Text.Wrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.65
                    }

                    ColumnLayout {
                        visible: delegateItem.author === "assistant"
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing / 2

                        RowLayout {
                            visible: delegateItem.reasoning.length > 0
                            Layout.fillWidth: true
                            spacing: 0

                            QQC2.ToolButton {
                                text: i18n("Thinking")
                                icon.name: "help-hint"
                                checkable: true
                                checked: delegateItem.thinkingExpanded
                                onClicked: delegateItem.thinkingExpanded = checked
                            }
                            QQC2.ToolButton {
                                icon.name: "edit-copy"
                                display: QQC2.AbstractButton.IconOnly
                                onClicked: root.copyText(delegateItem.reasoning)
                            }
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.preferredHeight: delegateItem.thinkingExpanded ? Math.min(implicitHeight, 220) : 0
                            visible: delegateItem.reasoning.length > 0
                            text: delegateItem.reasoning
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            opacity: 0.65
                            clip: true
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: delegateItem.text.length > 0
                            text: delegateItem.text
                            wrapMode: Text.Wrap
                            textFormat: delegateItem.streaming ? Text.PlainText : Text.MarkdownText
                            onLinkActivated: (link) => {
                                if (link.startsWith("okular-page:")) {
                                    root.ai.goToPage(parseInt(link.substring("okular-page:".length), 10));
                                }
                            }
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: delegateItem.text.length === 0 && delegateItem.streaming
                            text: i18n("Thinking…")
                            font.italic: true
                            opacity: 0.6
                        }

                        RowLayout {
                            visible: delegateItem.text.length > 0 && !delegateItem.streaming
                            QQC2.ToolButton {
                                icon.name: "edit-copy"
                                display: QQC2.AbstractButton.IconOnly
                                onClicked: root.copyText(delegateItem.text)
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 3
                visible: listView.count === 0
                icon.name: "assistant"
                text: i18n("Ask about this document")
                explanation: i18n("Summaries, explanations, questions and citations stay grounded in the open document.")
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                visible: listView.count === 0 && root.ai && !root.ai.busy
                spacing: Kirigami.Units.smallSpacing

                QQC2.Button {
                    Layout.fillWidth: true
                    text: i18n("Summarize")
                    onClicked: root.ai.send(i18n("Summarize this document and cite the relevant pages."))
                }
                QQC2.Button {
                    Layout.fillWidth: true
                    text: i18n("Key findings")
                    onClicked: root.ai.send(i18n("What are the key findings and conclusions of this document?"))
                }
                QQC2.Button {
                    Layout.fillWidth: true
                    text: i18n("Explain page")
                    onClicked: root.ai.send(i18n("Explain the current page in simple terms."))
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.ScrollView {
                    Layout.fillWidth: true
                    Layout.maximumHeight: Kirigami.Units.gridUnit * 5

                    QQC2.TextArea {
                        id: composer
                        placeholderText: i18n("Ask about this document…")
                        wrapMode: QQC2.TextArea.Wrap
                        enabled: !root.ai.busy
                        onTextChanged: {
                            if (contentHeight > 0) {
                                implicitHeight = Math.min(contentHeight + topPadding + bottomPadding, Kirigami.Units.gridUnit * 5);
                            }
                        }
                    }
                }

                QQC2.Button {
                    text: root.ai.busy ? i18n("Stop") : i18n("Send")
                    icon.name: root.ai.busy ? "process-stop" : "document-send"
                    enabled: root.ai.busy || composer.text.trim().length > 0
                    onClicked: {
                        if (root.ai.busy) {
                            root.ai.stop();
                        } else {
                            root.ai.send(composer.text);
                            composer.text = "";
                        }
                    }
                }
            }
        }
    }
}
