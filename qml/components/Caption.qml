import QtQuick
import BeldexGui

// Small uppercase Michroma label ("BLOCK HEIGHT", "STAKING REQUIREMENT", …)
Text {
    color: Theme.textFaint
    font.family: Theme.display
    font.pixelSize: 10
    font.letterSpacing: Theme.labelSpacing
    font.capitalization: Font.AllUppercase
    wrapMode: Text.WordWrap
    renderType: Text.NativeRendering
}
