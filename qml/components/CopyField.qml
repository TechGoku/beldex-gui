import QtQuick
import QtQuick.Layouts
import BeldexGui

// Long value (address, key, hash) in green mono with a COPY button.
ColumnLayout {
    id: root
    property alias label: cap.text
    property string value
    property bool secret: false
    property bool revealed: !secret
    property bool wrap: true
    property color valueColor: Theme.green
    spacing: 8
    Layout.fillWidth: true

    Caption { id: cap; visible: text.length > 0; Layout.fillWidth: true }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Math.max(Theme.controlHeight, valueText.implicitHeight + 22)
        color: "#080808"
        border.color: Theme.borderStrong
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 6
            spacing: 8
            Mono {
                id: valueText
                text: root.revealed ? root.value : root.value.replace(/./g, "•")
                color: root.valueColor
                font.pixelSize: 12
                wrapMode: root.wrap ? Text.WrapAnywhere : Text.NoWrap
                elide: root.wrap ? Text.ElideNone : Text.ElideMiddle
                Layout.fillWidth: true
                textFormat: Text.PlainText
            }
            BdxButton {
                visible: root.secret
                compact: true
                text: root.revealed ? "Hide" : "Show"
                onClicked: root.revealed = !root.revealed
            }
            BdxButton {
                compact: true
                text: copied.running ? "Copied" : "Copy"
                variant: copied.running ? "accent" : "ghost"
                onClicked: { WalletManager.copyToClipboard(root.value); copied.restart() }
                Timer { id: copied; interval: 1400 }
            }
        }
    }
}
