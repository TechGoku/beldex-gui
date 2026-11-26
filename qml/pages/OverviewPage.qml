import QtQuick
import QtQuick.Layouts
import BeldexGui

BdxPage {
    id: page
    readonly property var w: WalletManager.wallet

    title: "Wallet overview"
    subtitle: w ? "Account #" + w.accountIndex + " · " + w.address : ""

    headerRight: [
        BdxButton { text: App.settings.hideBalance ? "Show balance" : "Hide balance"; onClicked: App.settings.hideBalance = !App.settings.hideBalance },
        BdxButton { text: "Send"; variant: "accent"; onClicked: App.go("send") },
        BdxButton { text: "Receive"; variant: "primary"; onClicked: App.go("receive") }
    ]

    StatGrid {
        Layout.fillWidth: true
        columns: 4
        StatTile { label: "Balance"; value: App.money(page.w.balance); unit: "BDX"; valueColor: Theme.green }
        StatTile { label: "Unlocked"; value: App.money(page.w.unlockedBalance); unit: "BDX"; sub: "available to spend now" }
        StatTile { label: "Locked"; value: App.money(page.w.lockedBalance); unit: "BDX"; sub: "staked or awaiting unlock" }
        StatTile { label: "Staked"; value: App.money(page.w.stakedBalance); unit: "BDX"; sub: page.w.stakes.length + " master node stake" + (page.w.stakes.length === 1 ? "" : "s") }
        StatTile { label: "Wallet height"; value: App.fmtHeight(page.w.blockHeight) }
        StatTile {
            label: "Network height"
            value: page.w.daemonHeight > 0 ? App.fmtHeight(page.w.daemonHeight) : "—"
            sub: page.w.connected ? page.w.daemonAddress : "not connected"
        }
        StatTile {
            label: "Sync"
            value: !page.w.connected ? "offline" : page.w.synchronized ? "100" : (page.w.syncProgress * 100).toFixed(1)
            unit: page.w.connected ? "%" : ""
            valueColor: !page.w.connected ? Theme.red : page.w.synchronized ? Theme.green : Theme.amber
            sub: page.w.synchronized ? "up to date" : page.w.connected ? "scanning blocks…" : "check Settings › Node"
        }
        StatTile {
            label: "Hard fork"
            value: page.w.hardFork > 0 ? "v" + page.w.hardFork : "—"
            unit: page.w.netName.toLowerCase()
        }
    }

    // Testnet: privacy token balances of this account.
    Panel {
        title: "Privacy tokens"
        note: page.w.tokens.length + " held"
        visible: page.w.tokensEnabled
        Layout.fillWidth: true
        headerExtra: BdxButton { compact: true; text: "Manage"; onClicked: App.go("tokens") }
        Flow {
            Layout.fillWidth: true
            spacing: 28
            Repeater {
                model: page.w.tokens
                delegate: Row {
                    required property var modelData
                    spacing: 8
                    Mono { text: App.money(modelData.balance); color: Theme.green; font.bold: true; font.pixelSize: 15 }
                    Mono { text: modelData.ticker; color: Theme.textDim; font.pixelSize: 15 }
                }
            }
            Mono {
                visible: page.w.tokens.length === 0
                text: "No tokens in this account yet — create one or receive some at your address."
                color: Theme.textFaint
                font.italic: true
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 22

        Panel {
            title: "Recent transactions"
            note: page.w.history.totalCount + " total"
            contentPadding: 0
            spacing: 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            headerExtra: BdxButton { compact: true; text: "All"; onClicked: App.go("history") }
            TxTable {
                Layout.fillWidth: true
                model: page.w.recentTransactions
                showUnits: page.w.tokensEnabled
                scrollable: false
                emptyText: page.w.synchronized ? "No transactions yet — share your address to receive BDX" : "Transactions appear as the wallet syncs"
                onActivated: tx => App.showTx(tx)
            }
        }

        Panel {
            title: "Your address"
            Layout.preferredWidth: 330
            Layout.alignment: Qt.AlignTop
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 220
                implicitHeight: 220
                color: "white"
                Image {
                    anchors.fill: parent
                    anchors.margins: 6
                    source: page.w ? "image://qrcode/" + encodeURIComponent("beldex:" + page.w.address) : ""
                    sourceSize: Qt.size(420, 420)
                    smooth: false
                }
            }
            CopyField { value: page.w.address }
            Mono {
                text: "Use a fresh subaddress for each payer — see Receive."
                color: Theme.textFaint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
}
