import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BeldexGui

ColumnLayout {
    id: root
    property alias label: cap.text
    property alias text: area.text
    property alias placeholderText: area.placeholderText
    property alias readOnly: area.readOnly
    property string error: ""
    property string help: ""
    property int minHeight: 96

    spacing: 8
    Layout.fillWidth: true

    Caption { id: cap; visible: text.length > 0; Layout.fillWidth: true }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Math.max(root.minHeight, area.implicitHeight + 4)
        color: "#080808"
        border.width: 1
        border.color: root.error.length > 0 ? Theme.red : area.activeFocus ? Theme.green : Theme.borderStrong
        TextArea {
            id: area
            anchors.fill: parent
            anchors.margins: 2
            color: Theme.text
            placeholderTextColor: Theme.textFaint
            selectionColor: Theme.green
            selectedTextColor: Theme.controlFg
            font.family: Theme.mono
            font.pixelSize: 13
            wrapMode: TextEdit.Wrap
            leftPadding: 12
            rightPadding: 12
            topPadding: 10
            background: null
            selectByMouse: true
        }
    }

    Mono {
        text: root.error.length > 0 ? root.error : root.help
        visible: text.length > 0
        color: root.error.length > 0 ? Theme.red : Theme.textFaint
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
