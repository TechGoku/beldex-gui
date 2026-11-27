import QtQuick
import QtQuick.Layouts
import BeldexGui

// One row of a parameter table ("BASE FEE    0.0001 BDX / output …").
RowLayout {
    property alias key: k.text
    property alias value: v.text
    property alias valueColor: v.color
    property int keyWidth: 170
    spacing: 16
    Layout.fillWidth: true
    // Never narrower than the key itself: a wrapped key ("BLOCK / TIME") misaligns the row.
    Caption { id: k; Layout.preferredWidth: Math.max(parent.keyWidth, implicitWidth); Layout.alignment: Qt.AlignTop; Layout.topMargin: 3 }
    Mono {
        id: v
        Layout.fillWidth: true
        wrapMode: Text.WrapAnywhere
        textFormat: Text.PlainText
    }
}
