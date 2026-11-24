import QtQuick
import BeldexGui

// Small outlined tag for a transaction type.
Rectangle {
    property string kind: "in"
    property bool pending: false
    readonly property color tone: pending ? Theme.amber : Theme.kindColor(kind)
    implicitWidth: t.implicitWidth + 14
    implicitHeight: 20
    color: "transparent"
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.45)
    Text {
        id: t
        anchors.centerIn: parent
        text: parent.pending ? "PENDING" : Theme.kindLabel(parent.kind)
        color: parent.tone
        font.family: Theme.display
        font.pixelSize: 8
        font.letterSpacing: 1.4
        renderType: Text.NativeRendering
    }
}
