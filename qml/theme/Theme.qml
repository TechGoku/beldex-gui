pragma Singleton
import QtQuick

// Design tokens taken from explorer.beldex.io (static/style.css :root).
QtObject {
    // surfaces
    readonly property color bg: "#0a0a0a"
    readonly property color panel: "#0d0d0d"
    readonly property color header: "#050505"
    readonly property color row: "#242424"          // table header row / hovered row
    readonly property color rowHover: "#151515"
    readonly property color border: "#1e1e1e"
    readonly property color borderStrong: "#2c2c2c"

    // text
    readonly property color text: "#f2f2f2"
    readonly property color textDim: "#8f8f8f"
    readonly property color textFaint: "#5c5c5c"

    // accents
    readonly property color green: "#00d959"
    readonly property color greenNav: "#00c853"
    readonly property color greenDim: Qt.rgba(0, 217 / 255, 89 / 255, 0.12)
    readonly property color red: "#ff4d5b"
    readonly property color amber: "#ffb020"
    readonly property color blue: "#2f7dff"
    readonly property color controlBg: "#ffffff"
    readonly property color controlFg: "#000000"

    // type
    readonly property string display: "Michroma"
    readonly property string mono: "Space Mono"

    readonly property int fontTiny: 10
    readonly property int fontSmall: 12
    readonly property int fontBody: 13
    readonly property int fontValue: 21
    readonly property int fontTitle: 30

    readonly property real labelSpacing: 2.4   // letter-spacing of Michroma labels
    readonly property real headingSpacing: 2.6

    // layout
    readonly property int gutter: 24
    readonly property int pad: 18
    readonly property int controlHeight: 40
    readonly property int maxContentWidth: 1240

    function kindColor(kind) {
        switch (kind) {
        case "in": case "reward": case "miner": return green
        case "out": return text
        case "stake": return blue
        case "bns": return amber
        case "token-register": case "token-mint": return green
        case "token-burn": return red
        case "token-update": return blue
        }
        return textDim
    }
    function kindLabel(kind) {
        switch (kind) {
        case "in": return "RECEIVED"
        case "out": return "SENT"
        case "reward": return "MN REWARD"
        case "miner": return "MINER REWARD"
        case "stake": return "STAKE"
        case "bns": return "BNS"
        case "token-register": return "NEW TOKEN"
        case "token-mint": return "MINTED"
        case "token-burn": return "BURNED"
        case "token-update": return "UPDATED"
        }
        return kind.toUpperCase()
    }
}
