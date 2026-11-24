import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BeldexGui

// Square toggle with Michroma caption + mono description.
AbstractButton {
    id: control
    property string description: ""
    checkable: true
    hoverEnabled: true
    implicitHeight: row.implicitHeight
    implicitWidth: row.implicitWidth

    contentItem: RowLayout {
        id: row
        spacing: 14
        Rectangle {
            implicitWidth: 38
            implicitHeight: 20
            color: control.checked ? Theme.greenDim : "transparent"
            border.color: control.checked ? Theme.green : Theme.borderStrong
            Rectangle {
                width: 12; height: 12
                y: 4
                x: control.checked ? parent.width - width - 4 : 4
                color: control.checked ? Theme.green : Theme.textFaint
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true
            Text {
                text: control.text
                color: control.checked ? Theme.text : Theme.textDim
                font.family: Theme.display
                font.pixelSize: 10
                font.letterSpacing: 1.8
                font.capitalization: Font.AllUppercase
                renderType: Text.NativeRendering
            }
            Mono {
                text: control.description
                visible: text.length > 0
                color: Theme.textFaint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
    background: null
}
