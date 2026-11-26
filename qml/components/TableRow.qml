import QtQuick
import QtQuick.Layouts
import BeldexGui

// One table row under a TableHeader; children are laid out left to right.
Rectangle {
    id: root
    default property alias cells: row.data
    property bool clickable: false
    signal clicked()
    Layout.fillWidth: true
    implicitHeight: 46
    color: clickable && ma.containsMouse ? Theme.rowHover : "transparent"
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    MouseArea {
        id: ma
        anchors.fill: parent
        enabled: root.clickable
        hoverEnabled: root.clickable
        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.pad
        anchors.rightMargin: Theme.pad
        spacing: 12
    }
}
