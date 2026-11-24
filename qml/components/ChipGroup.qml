import QtQuick
import QtQuick.Layouts
import BeldexGui

// Row of mutually exclusive chips: model = [{value, label}] or ["A", "B"].
RowLayout {
    id: root
    property var model: []
    property var current
    signal picked(var value)
    spacing: 0

    Repeater {
        model: root.model
        delegate: Rectangle {
            required property var modelData
            required property int index
            readonly property var value: typeof modelData === "object" ? modelData.value : modelData
            readonly property string label: typeof modelData === "object" ? modelData.label : String(modelData)
            readonly property bool active: root.current === value
            implicitHeight: 32
            implicitWidth: txt.implicitWidth + 28
            color: active ? Theme.greenDim : ma.containsMouse ? "#121212" : "transparent"
            border.width: 1
            border.color: active ? Theme.green : Theme.borderStrong
            // collapse double borders between neighbours
            Layout.leftMargin: index === 0 ? 0 : -1
            z: active ? 1 : 0
            Text {
                id: txt
                anchors.centerIn: parent
                text: parent.label
                color: parent.active ? Theme.green : ma.containsMouse ? Theme.text : Theme.textDim
                font.family: Theme.display
                font.pixelSize: 10
                font.letterSpacing: 1.8
                font.capitalization: Font.AllUppercase
                renderType: Text.NativeRendering
            }
            MouseArea {
                id: ma
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: { root.current = parent.value; root.picked(parent.value) }
            }
        }
    }
}
