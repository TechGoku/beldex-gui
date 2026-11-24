import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BeldexGui

// Modal panel: heading, body (default children), footer buttons.
Popup {
    id: root
    property string title
    default property alias body: content.data
    property alias footer: footerRow.data
    property int dialogWidth: 560

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(dialogWidth, (parent ? parent.width : dialogWidth) - 48)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.72) }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }

    background: Rectangle {
        color: Theme.panel
        border.color: Theme.borderStrong
        Rectangle { width: parent.width; height: 2; color: Theme.green }
    }

    contentItem: ColumnLayout {
        spacing: 0
        Text {
            text: root.title
            color: Theme.text
            font.family: Theme.display
            font.pixelSize: 13
            font.letterSpacing: Theme.headingSpacing
            font.capitalization: Font.AllUppercase
            renderType: Text.NativeRendering
            Layout.fillWidth: true
            Layout.margins: 22
            Layout.bottomMargin: 16
            wrapMode: Text.WordWrap
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        ColumnLayout {
            id: content
            Layout.fillWidth: true
            Layout.margins: 22
            spacing: 14
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; visible: footerRow.children.length > 0 }
        RowLayout {
            id: footerRow
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            spacing: 10
            visible: children.length > 0
        }
    }
}
