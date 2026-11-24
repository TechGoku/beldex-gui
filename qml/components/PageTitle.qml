import QtQuick
import QtQuick.Layouts
import BeldexGui

// Big Michroma page heading + mono subtitle ("BLOCKCHAIN EXPLORER" / "Server time: …")
ColumnLayout {
    property alias title: t.text
    property alias subtitle: s.text
    spacing: 10

    Text {
        id: t
        color: Theme.text
        font.family: Theme.display
        font.pixelSize: Theme.fontTitle
        font.letterSpacing: 1.6
        font.capitalization: Font.AllUppercase
        renderType: Text.NativeRendering
        Layout.fillWidth: true
        elide: Text.ElideRight
    }
    Mono {
        id: s
        color: Theme.textDim
        font.pixelSize: 11
        visible: text.length > 0
        Layout.fillWidth: true
        elide: Text.ElideMiddle
    }
}
