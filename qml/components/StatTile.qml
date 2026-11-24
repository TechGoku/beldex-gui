import QtQuick
import QtQuick.Layouts
import BeldexGui

// LABEL / big mono value / small unit / optional sub-line. Same as the explorer's stat cells.
Rectangle {
    id: root
    property string label
    property string value
    property string unit
    property string sub
    property color valueColor: Theme.text
    property bool loading: false

    color: Theme.panel
    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.preferredWidth: 100
    implicitHeight: col.implicitHeight + 44

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        anchors.topMargin: 22
        spacing: 10

        Caption { text: root.label; Layout.fillWidth: true }
        RowLayout {
            spacing: 6
            Layout.fillWidth: true
            Mono {
                text: root.loading ? "—" : root.value
                color: root.valueColor
                font.pixelSize: Theme.fontValue
                font.bold: true
                elide: Text.ElideRight
                Layout.maximumWidth: col.width - unitText.width - 6
            }
            Mono {
                id: unitText
                text: root.unit
                visible: text.length > 0
                color: Theme.textDim
                font.pixelSize: 11
                Layout.alignment: Qt.AlignBaseline
            }
        }
        Mono {
            text: root.sub
            visible: text.length > 0
            color: Theme.textDim
            font.pixelSize: 11
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }
}
