import QtQuick
import QtQuick.Layouts
import BeldexGui

// Explorer-style table header row. cols: [[title, width]], width -1 fills.
Rectangle {
    id: root
    property var cols: []
    Layout.fillWidth: true
    implicitHeight: 38
    color: Theme.row
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pad
        anchors.rightMargin: Theme.pad
        spacing: 12
        Repeater {
            model: root.cols
            Mono {
                required property var modelData
                required property int index
                text: modelData[0]
                color: Theme.text
                Layout.preferredWidth: modelData[1] > 0 ? modelData[1] : -1
                Layout.fillWidth: modelData[1] < 0
                horizontalAlignment: index > 0 && index < root.cols.length - 1 ? Text.AlignRight : Text.AlignLeft
            }
        }
    }
}
