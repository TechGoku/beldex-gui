import QtQuick
import QtQuick.Layouts
import BeldexGui

// Bottom-right stack of short notifications. Call show(text, tone) with tone "ok" | "error" | "info".
Item {
    id: root
    function show(text, tone) { toastModel.insert(0, { text: text, tone: tone || "info" }) }

    ListModel { id: toastModel }

    Column {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        spacing: 8
        Repeater {
            model: toastModel
            delegate: Rectangle {
                id: toast
                required property string text
                required property string tone
                required property int index
                readonly property color accent: tone === "error" ? Theme.red : tone === "ok" ? Theme.green : Theme.textDim
                width: Math.min(420, msg.implicitWidth + 48)
                height: msg.implicitHeight + 26
                color: Theme.panel
                border.color: Qt.rgba(accent.r, accent.g, accent.b, 0.6)
                Rectangle { width: 3; height: parent.height; color: toast.accent }
                Mono {
                    id: msg
                    anchors.verticalCenter: parent.verticalCenter
                    x: 20
                    width: Math.min(372, implicitWidth)
                    text: toast.text
                    wrapMode: Text.WordWrap
                    color: Theme.text
                }
                Timer { running: true; interval: toast.tone === "error" ? 6500 : 3500; onTriggered: toastModel.remove(toast.index) }
                MouseArea { anchors.fill: parent; onClicked: toastModel.remove(toast.index) }
            }
        }
    }
}
