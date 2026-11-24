import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BeldexGui

// Caption + field + error/help line. `trailing` holds small inline buttons (PASTE, MAX, …).
ColumnLayout {
    id: root
    property alias label: cap.text
    property alias text: field.text
    property alias placeholderText: field.placeholderText
    property alias echoMode: field.echoMode
    property alias validator: field.validator
    property alias inputMethodHints: field.inputMethodHints
    property alias readOnly: field.readOnly
    property alias field: field
    property alias trailing: trailingRow.data
    property string error: ""
    property string help: ""
    property string suffix: ""
    property bool monoSmall: false
    signal accepted()

    spacing: 8
    Layout.fillWidth: true

    Caption { id: cap; visible: text.length > 0; Layout.fillWidth: true }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        color: "#080808"
        border.width: 1
        border.color: root.error.length > 0 ? Theme.red : field.activeFocus ? Theme.green : Theme.borderStrong

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 6
            spacing: 6
            TextField {
                id: field
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.text
                placeholderTextColor: Theme.textFaint
                selectionColor: Theme.green
                selectedTextColor: Theme.controlFg
                font.family: Theme.mono
                font.pixelSize: root.monoSmall ? 12 : 13
                leftPadding: 0
                rightPadding: 0
                verticalAlignment: TextInput.AlignVCenter
                background: null
                selectByMouse: true
                onAccepted: root.accepted()
            }
            Mono {
                text: root.suffix
                visible: text.length > 0
                color: Theme.textDim
                font.pixelSize: 12
                Layout.rightMargin: 8
            }
            RowLayout { id: trailingRow; spacing: 4 }
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
