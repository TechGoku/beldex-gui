import QtQuick
import QtQuick.Controls.Basic
import BeldexGui

// variant: "primary" (white block, like SEARCH), "accent" (green outline, like the active nav
// tab / MAINNET chip), "ghost" (grey outline, like AUTOREFRESH), "danger" (red outline).
Button {
    id: control
    property string variant: "ghost"
    property bool compact: false

    readonly property color fg: !enabled ? Theme.textFaint
        : variant === "primary" ? Theme.controlFg
        : variant === "accent" ? Theme.green
        : variant === "danger" ? Theme.red
        : (hovered ? Theme.text : Theme.textDim)

    implicitHeight: compact ? 30 : Theme.controlHeight
    leftPadding: compact ? 12 : 26
    rightPadding: compact ? 12 : 26
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    contentItem: Text {
        text: control.text
        color: control.fg
        font.family: control.variant === "primary" ? Theme.mono : Theme.display
        font.pixelSize: control.variant === "primary" ? 13 : (control.compact ? 10 : 11)
        font.bold: control.variant === "primary"
        font.letterSpacing: control.variant === "primary" ? 0.5 : 1.9
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        renderType: Text.NativeRendering
    }

    background: Rectangle {
        color: control.variant === "primary"
               ? (!control.enabled ? Theme.borderStrong : control.down ? "#d9d9d9" : control.hovered ? "#ececec" : Theme.controlBg)
               : control.variant === "accent" && (control.hovered || control.checked) ? Theme.greenDim
               : control.hovered ? "#121212" : "transparent"
        border.width: control.variant === "primary" ? 0 : 1
        border.color: control.variant === "accent" ? (control.enabled ? Theme.green : Theme.borderStrong)
                    : control.variant === "danger" ? (control.enabled ? Theme.red : Theme.borderStrong)
                    : control.visualFocus ? Theme.green : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
