import QtQuick
import QtQuick.Layouts
import BeldexGui

BdxPage {
    id: page
    readonly property var w: WalletManager.wallet

    title: "Master nodes"
    subtitle: "Stake BDX to a master node to earn a share of its block rewards. Your BDX never leave your wallet."

    StatGrid {
        Layout.fillWidth: true
        columns: 4
        StatTile { label: "Staked by you"; value: App.money(page.w.stakedBalance); unit: "BDX"; valueColor: Theme.green }
        StatTile { label: "Your stakes"; value: String(page.w.stakes.length); sub: page.w.stakes.filter(s => s.state === "active").length + " active" }
        StatTile { label: "Staking requirement"; value: "10000"; unit: "BDX"; sub: "full master node (operators can share it)" }
        StatTile { label: "Available to stake"; value: App.money(page.w.unlockedBalance); unit: "BDX"; sub: "unlocked balance" }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 22

        Panel {
            title: "Your stakes"
            contentPadding: 0
            spacing: 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 38
                color: Theme.row
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.pad
                    anchors.rightMargin: Theme.pad
                    spacing: 12
                    Mono { text: "Master node public key"; Layout.fillWidth: true }
                    Mono { text: "Stake (BDX)"; Layout.preferredWidth: 120; horizontalAlignment: Text.AlignRight }
                    Mono { text: "Status"; Layout.preferredWidth: 130 }
                    Item { Layout.preferredWidth: 120 }
                }
            }
            Repeater {
                model: page.w.stakes
                delegate: Rectangle {
                    id: st
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 52
                    color: "transparent"
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.pad
                        anchors.rightMargin: Theme.pad
                        spacing: 12
                        Mono {
                            text: st.modelData.masterNodeKey
                            color: Theme.green
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: App.openMasterNode(st.modelData.masterNodeKey) }
                        }
                        Mono { text: st.modelData.amount; font.bold: true; Layout.preferredWidth: 120; horizontalAlignment: Text.AlignRight }
                        ColumnLayout {
                            Layout.preferredWidth: 130
                            spacing: 2
                            Mono {
                                text: st.modelData.state
                                color: st.modelData.state === "active" ? Theme.green
                                     : st.modelData.state === "decommissioned" ? Theme.red : Theme.amber
                                font.capitalization: Font.AllUppercase
                                font.pixelSize: 11
                            }
                            Mono {
                                visible: st.modelData.unlockHeight > 0
                                text: "unlocks " + App.heightToDate(st.modelData.unlockHeight)
                                color: Theme.textFaint
                                font.pixelSize: 10
                            }
                        }
                        BdxButton {
                            compact: true
                            text: "Unlock"
                            variant: "danger"
                            enabled: st.modelData.unlockHeight === 0 && !page.w.busy
                            Layout.preferredWidth: 120
                            onClicked: page.w.requestStakeUnlock(st.modelData.masterNodeKey)
                        }
                    }
                }
            }
            Mono {
                visible: page.w.stakes.length === 0
                text: page.w.synchronized ? "You are not staking to any master node." : "Stakes appear after the wallet has synced."
                color: Theme.textFaint
                font.italic: true
                Layout.margins: Theme.pad
            }
        }

        Panel {
            title: "Stake to a master node"
            Layout.preferredWidth: 420
            Layout.alignment: Qt.AlignTop

            BdxTextField {
                id: mnKey
                label: "Master node public key"
                placeholderText: "64 hex characters"
                monoSmall: true
                error: text.length > 0 && !WalletManager.masterNodeKeyValid(text) ? "Must be 64 hexadecimal characters" : ""
                help: "Find nodes that are open for contributions on explorer.beldex.io › Master nodes."
            }
            BdxTextField {
                id: stakeAmount
                label: "Amount"
                placeholderText: "0.00"
                suffix: "BDX"
                validator: RegularExpressionValidator { regularExpression: /^\d{0,10}(\.\d{0,9})?$/ }
                help: "Available: " + App.money(page.w.unlockedBalance) + " BDX"
                trailing: BdxButton {
                    compact: true
                    text: "Max"
                    onClicked: stakeAmount.text = page.w.unlockedBalance.replace(/,/g, "")
                }
            }
            BdxButton {
                text: "Review stake"
                variant: "primary"
                Layout.fillWidth: true
                enabled: WalletManager.masterNodeKeyValid(mnKey.text) && WalletManager.amountValid(stakeAmount.text)
                         && page.w.synchronized && !page.w.busy
                onClicked: page.w.stake(mnKey.text, stakeAmount.text)
            }
            Mono {
                text: "The node operator sets the minimum contribution and fee. Staked BDX stay locked until you request an unlock and the unlock period has passed."
                color: Theme.textFaint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    Connections {
        target: page.w
        function onTransactionCommitted(success) { if (success) { mnKey.text = ""; stakeAmount.text = "" } }
    }
}
