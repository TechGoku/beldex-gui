import QtQuick
import QtQuick.Layouts
import BeldexGui

// Bordered panel with an optional Michroma heading ("NETWORK PARAMETERS") and a right-aligned
// dim note ("0 transactions · 0B"). Children go into a padded ColumnLayout.
Rectangle {
    id: root
    property string title: ""
    property string note: ""
    property alias headerExtra: extra.data     // e.g. buttons next to the note
    property int contentPadding: Theme.pad
    property alias spacing: body.spacing
    default property alias content: body.data

    color: Theme.panel
    border.color: Theme.border
    border.width: 1
    implicitHeight: col.implicitHeight + 2
    implicitWidth: 300

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        RowLayout {
            visible: root.title.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pad
            Layout.rightMargin: Theme.pad
            Layout.topMargin: 16
            Layout.bottomMargin: 14
            spacing: 12
            Text {
                text: root.title
                color: Theme.text
                font.family: Theme.display
                font.pixelSize: 12
                font.letterSpacing: Theme.headingSpacing
                font.capitalization: Font.AllUppercase
                renderType: Text.NativeRendering
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Mono {
                text: root.note
                visible: text.length > 0
                color: Theme.textFaint
                font.pixelSize: 11
            }
            RowLayout { id: extra; spacing: 8 }
        }
        Rectangle {
            visible: root.title.length > 0
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: root.contentPadding
            spacing: 14
        }
    }
}
