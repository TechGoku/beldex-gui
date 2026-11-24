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
    Caption { id: k; Layout.preferredWidth: parent.keyWidth; Layout.alignment: Qt.AlignTop; Layout.topMargin: 3 }
    Mono {
        id: v
        Layout.fillWidth: true
        wrapMode: Text.WrapAnywhere
        textFormat: Text.PlainText
    }
}
