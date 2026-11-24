import QtQuick
import QtQuick.Layouts
import BeldexGui

BdxPage {
    id: page
    readonly property var w: WalletManager.wallet

    title: "Contacts"
    subtitle: "Saved addresses. Stored encrypted inside this wallet file."

    RowLayout {
        Layout.fillWidth: true
        spacing: 22

        Panel {
            title: "Address book"
            note: page.w.addressBook.length + " saved"
            contentPadding: 0
            spacing: 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop

            Repeater {
                model: page.w.addressBook
                delegate: Rectangle {
                    id: c
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 56
                    color: cm.containsMouse ? Theme.rowHover : "transparent"
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                    MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.pad
                        anchors.rightMargin: Theme.pad
                        spacing: 14
                        Mono { text: c.modelData.description || "—"; font.bold: true; Layout.preferredWidth: 180; elide: Text.ElideRight }
                        Mono { text: c.modelData.address; color: Theme.green; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        BdxButton { compact: true; text: "Send"; variant: "accent"; onClicked: App.go("send", { address: c.modelData.address }) }
                        BdxButton { compact: true; text: "Copy"; onClicked: { WalletManager.copyToClipboard(c.modelData.address); App.toast("Address copied", "ok") } }
                        BdxButton { compact: true; text: "Delete"; variant: "danger"; onClicked: page.w.deleteAddressBookEntry(c.modelData.rowId) }
                    }
                }
            }
            Mono {
                visible: page.w.addressBook.length === 0
                text: "No contacts yet."
                color: Theme.textFaint
                font.italic: true
                Layout.margins: Theme.pad
            }
        }

        Panel {
            title: "Add contact"
            Layout.preferredWidth: 400
            Layout.alignment: Qt.AlignTop
            BdxTextField { id: name; label: "Name"; placeholderText: "Alice" }
            BdxTextField {
                id: addr
                label: "Address"
                placeholderText: "bx…"
                monoSmall: true
                error: text.length > 0 && !WalletManager.addressValid(text, page.w.netType) ? "Not a valid " + page.w.netName.toLowerCase() + " address" : ""
            }
            BdxButton {
                text: "Save contact"
                variant: "primary"
                Layout.fillWidth: true
                enabled: name.text.length > 0 && WalletManager.addressValid(addr.text, page.w.netType)
                onClicked: {
                    if (page.w.addAddressBookEntry(addr.text, name.text)) {
                        App.toast("Contact saved", "ok")
                        name.text = ""
                        addr.text = ""
                    }
                }
            }
        }
    }
}
