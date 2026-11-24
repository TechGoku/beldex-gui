import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BeldexGui

BdxPage {
    id: page
    readonly property var w: WalletManager.wallet
    property int selected: 0
    readonly property var current: w && w.subaddresses.length > selected ? w.subaddresses[selected] : ({ address: "", label: "", index: 0 })
    readonly property string uri: w ? w.makePaymentUri(current.address, amount.text, memo.text) : ""

    title: "Receive BDX"
    subtitle: "Every subaddress points to this wallet. Use a new one per payer so payments can't be linked."

    headerRight: [
        ComboBox {
            id: accountBox
            visible: page.w && page.w.accounts.length > 1
            model: page.w ? page.w.accounts.map(a => "#" + a.index + "  " + (a.label || "Account")) : []
            currentIndex: page.w ? page.w.accountIndex : 0
            onActivated: i => { page.w.accountIndex = i; page.selected = 0 }
            implicitWidth: 220
        },
        BdxButton {
            text: "+ Account"
            onClicked: { page.w.addAccount("Account " + page.w.accounts.length); App.toast("Account created", "ok") }
        }
    ]

    RowLayout {
        Layout.fillWidth: true
        spacing: 22

        Panel {
            title: page.current.index === 0 ? "Primary address" : "Subaddress #" + page.current.index
            note: page.current.label
            Layout.preferredWidth: 420
            Layout.alignment: Qt.AlignTop

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 260
                implicitHeight: 260
                color: "white"
                Image {
                    anchors.fill: parent
                    anchors.margins: 8
                    source: page.uri.length ? "image://qrcode/" + encodeURIComponent(page.uri) : ""
                    sourceSize: Qt.size(520, 520)
                    smooth: false
                }
            }
            CopyField { label: "Address"; value: page.current.address }
            RowLayout {
                spacing: 12
                Layout.fillWidth: true
                BdxTextField {
                    id: amount
                    label: "Amount (optional)"
                    placeholderText: "0.00"
                    suffix: "BDX"
                    validator: RegularExpressionValidator { regularExpression: /^\d{0,10}(\.\d{0,9})?$/ }
                    Layout.preferredWidth: 170
                }
                BdxTextField { id: memo; label: "Description"; placeholderText: "Invoice #42"; Layout.fillWidth: true }
            }
            CopyField {
                label: "Payment link"
                value: page.uri
                visible: amount.text.length > 0 || memo.text.length > 0
                valueColor: Theme.textDim
            }
        }

        Panel {
            title: "Subaddresses"
            note: page.w ? page.w.subaddresses.length + " in this account" : ""
            contentPadding: 0
            spacing: 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            headerExtra: BdxButton {
                compact: true
                variant: "accent"
                text: "+ New subaddress"
                onClicked: {
                    page.w.addSubaddress("")
                    page.selected = page.w.subaddresses.length
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 38
                color: Theme.row
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.pad
                    anchors.rightMargin: Theme.pad
                    spacing: 12
                    Mono { text: "#"; Layout.preferredWidth: 34 }
                    Mono { text: "Label"; Layout.preferredWidth: 170 }
                    Mono { text: "Address"; Layout.fillWidth: true }
                }
            }
            Repeater {
                model: page.w ? page.w.subaddresses : []
                delegate: Rectangle {
                    id: subRow
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: 46
                    color: page.selected === index ? Theme.greenDim : subMouse.containsMouse ? Theme.rowHover : "transparent"
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                    MouseArea {
                        id: subMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.selected = subRow.index
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.pad
                        anchors.rightMargin: Theme.pad
                        spacing: 12
                        Mono { text: subRow.modelData.index; color: Theme.textDim; Layout.preferredWidth: 34 }
                        TextInput {
                            text: subRow.modelData.label
                            color: Theme.text
                            font.family: Theme.mono
                            font.pixelSize: 12
                            Layout.preferredWidth: 170
                            clip: true
                            selectByMouse: true
                            selectionColor: Theme.green
                            Mono {
                                visible: parent.text.length === 0 && !parent.activeFocus
                                text: "add label…"
                                color: Theme.textFaint
                                font.italic: true
                            }
                            onEditingFinished: if (text !== subRow.modelData.label) page.w.setSubaddressLabel(subRow.modelData.index, text)
                        }
                        Mono {
                            text: subRow.modelData.address
                            color: Theme.green
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        BdxButton {
                            compact: true
                            text: "Copy"
                            onClicked: { WalletManager.copyToClipboard(subRow.modelData.address); App.toast("Address copied", "ok") }
                        }
                    }
                }
            }
        }
    }
}
