import QtQuick
import QtQuick.Layouts
import BeldexGui

// Grid of StatTiles separated by 1px lines (the explorer's stats block).
Rectangle {
    id: root
    property int columns: 4
    default property alias tiles: grid.data
    color: Theme.border
    implicitHeight: grid.implicitHeight + 2
    implicitWidth: grid.implicitWidth + 2

    GridLayout {
        id: grid
        anchors.fill: parent
        anchors.margins: 1
        columns: root.columns
        columnSpacing: 1
        rowSpacing: 1
    }
}
