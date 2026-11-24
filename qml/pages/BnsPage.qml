import QtQuick
import QtQuick.Layouts
import BeldexGui

BdxPage {
    id: page
    readonly property var w: WalletManager.wallet
    property string years: "1y"
    property var editing: null       // record being updated, or null for a new purchase

    title: "Beldex Name Service"
    subtitle: "Human-readable .bdx names for your wallet, BChat ID and BelNet address."

    function startUpdate(rec) {
        editing = rec
        nameField.text = rec.name
        bchat.text = rec.bchat
        walletAddr.text = rec.wallet
        belnet.text = rec.belnet
        eth.text = rec.eth
        owner.text = ""
        backup.text = rec.backupOwner
    }
    function clearForm() {
        editing = null
        for (const f of [nameField, bchat, walletAddr, belnet, eth, owner, backup]) f.text = ""
    }
    function fields() {
        return { name: nameField.text, years: page.years, owner: owner.text, backupOwner: backup.text,
                 bchat: bchat.text, wallet: walletAddr.text, belnet: belnet.text, eth: eth.text }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 22

        Panel {
            title: "Your names"
            note: page.w.bnsRecords.length + " record" + (page.w.bnsRecords.length === 1 ? "" : "s")
            contentPadding: 0
            spacing: 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop

            Repeater {
                model: page.w.bnsRecords
                delegate: Rectangle {
                    id: rec
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: recCol.implicitHeight + 28
                    color: "transparent"
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                    ColumnLayout {
                        id: recCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Theme.pad
                        anchors.rightMargin: Theme.pad
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Mono { text: rec.modelData.name; color: Theme.green; font.pixelSize: 16; font.bold: true }
                            Mono {
                                text: "expires ≈ " + App.heightToDate(rec.modelData.expirationHeight) + "  (block " + App.fmtHeight(rec.modelData.expirationHeight) + ")"
                                color: Theme.textDim
                                font.pixelSize: 11
                                Layout.fillWidth: true
                            }
                            BdxButton { compact: true; text: "Update"; onClicked: page.startUpdate(rec.modelData) }
                            BdxButton { compact: true; text: "Renew 1y"; variant: "accent"; onClicked: page.w.renewBns(rec.modelData.name, "1y") }
                        }
                        KeyValue { key: "BChat ID"; keyWidth: 110; value: rec.modelData.bchat; visible: rec.modelData.bchat.length > 0 }
                        KeyValue { key: "Wallet"; keyWidth: 110; value: rec.modelData.wallet; visible: rec.modelData.wallet.length > 0 }
                        KeyValue { key: "BelNet"; keyWidth: 110; value: rec.modelData.belnet; visible: rec.modelData.belnet.length > 0 }
                        KeyValue { key: "ETH"; keyWidth: 110; value: rec.modelData.eth; visible: rec.modelData.eth.length > 0 }
                    }
                }
            }
            Mono {
                visible: page.w.bnsRecords.length === 0
                text: "You don't own any BNS names yet."
                color: Theme.textFaint
                font.italic: true
                Layout.margins: Theme.pad
            }
        }

        Panel {
            title: page.editing ? "Update " + page.editing.name : "Register a name"
            Layout.preferredWidth: 440
            Layout.alignment: Qt.AlignTop
            headerExtra: BdxButton { compact: true; text: "Cancel"; visible: page.editing !== null; onClicked: page.clearForm() }

            BdxTextField {
                id: nameField
                label: "Name"
                placeholderText: "yourname"
                suffix: text.endsWith(".bdx") ? "" : ".bdx"
                readOnly: page.editing !== null
                validator: RegularExpressionValidator { regularExpression: /^[a-zA-Z0-9-]{0,63}(\.bdx)?$/ }
            }
            ColumnLayout {
                visible: page.editing === null
                spacing: 8
                Caption { text: "Duration" }
                ChipGroup {
                    current: page.years
                    model: [{ value: "1y", label: "1 yr" }, { value: "2y", label: "2 yrs" },
                            { value: "5y", label: "5 yrs" }, { value: "10y", label: "10 yrs" }]
                    onPicked: v => page.years = v
                }
            }
            BdxTextField { id: bchat; label: "BChat ID"; placeholderText: "bd…"; monoSmall: true }
            BdxTextField { id: walletAddr; label: "Wallet address"; placeholderText: "bx…"; monoSmall: true
                trailing: BdxButton { compact: true; text: "Mine"; onClicked: walletAddr.text = page.w.primaryAddress } }
            BdxTextField { id: belnet; label: "BelNet address"; placeholderText: "….bdx"; monoSmall: true }
            BdxTextField { id: eth; label: "ETH address"; placeholderText: "0x…"; monoSmall: true }
            BdxTextField { id: owner; label: "Owner (optional)"; placeholderText: "defaults to this wallet"; monoSmall: true }
            BdxTextField { id: backup; label: "Backup owner (optional)"; placeholderText: "another wallet that can update the record"; monoSmall: true }

            BdxButton {
                text: page.editing ? "Review update" : "Review purchase"
                variant: "primary"
                Layout.fillWidth: true
                enabled: nameField.text.length > 0 && page.w.synchronized && !page.w.busy
                onClicked: page.editing ? page.w.updateBns(page.fields()) : page.w.buyBns(page.fields())
            }
            Mono {
                text: "The registration fee is burned and shown before you confirm. Values are encrypted with the name, so only people who know the name can look them up."
                color: Theme.textFaint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    Connections {
        target: page.w
        function onTransactionCommitted(success) { if (success) page.clearForm() }
    }
}
