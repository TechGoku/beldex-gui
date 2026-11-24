import QtQuick
import BeldexGui

// The explorer's dotted grid: 1.2px dots every 30px on #0a0a0a.
Rectangle {
    color: Theme.bg
    Image {
        anchors.fill: parent
        source: "qrc:/qt/qml/BeldexGui/images/dot-tile.png"
        fillMode: Image.Tile
        smooth: false
    }
}
