import QtQuick
import QtQuick.Layouts
import BeldexGui

// Shows a PendingTransaction and commits it on confirm.
BdxDialog {
    id: root
    property var tx: null
    property bool committing: false
    readonly property bool bdx: !tx || tx.unit === "BDX"

    title: !tx ? "" : {
        "transfer": "Confirm transaction", "sweep": "Confirm sweep", "stake": "Confirm stake",
        "unlock": "Request stake unlock", "bns-buy": "Confirm BNS purchase",
        "bns-renew": "Confirm BNS renewal", "bns-update": "Confirm BNS update",
        "token-register": "Confirm token registration", "token-mint": "Confirm mint",
        "token-burn": "Confirm burn", "token-update": "Confirm token update"
    }[tx.kind] || "Confirm"

    function show(t) { tx = t; committing = false; open() }

    onClosed: {
        if (tx && !committing && WalletManager.wallet)
            WalletManager.wallet.discardTransaction(tx)
        tx = null
    }

    KeyValue {
        key: !root.tx ? "" : root.tx.kind === "stake" ? "Stake amount" : root.tx.kind === "token-register" ? "Initial supply"
           : root.tx.kind === "token-mint" ? "Mint" : root.tx.kind === "token-burn" ? "Burn" : "Amount"
        value: !root.tx ? "" : root.bdx ? root.tx.amount + " BDX" : root.tx.amount + (root.tx.unit ? " " + root.tx.unit : "")
        valueColor: root.bdx ? Theme.text : Theme.green
        visible: root.tx && root.tx.kind !== "unlock" && root.tx.kind !== "bns-update" && root.tx.kind !== "token-update"
    }
    KeyValue { key: "Network fee"; value: root.tx ? root.tx.fee + " BDX" : "" }
    KeyValue {
        key: "Total"
        value: root.tx ? root.tx.total + " BDX" : ""
        valueColor: Theme.green
        visible: root.tx && root.bdx && root.tx.kind !== "unlock" && root.tx.kind !== "bns-update"
    }
    KeyValue {
        key: root.tx && root.tx.kind.indexOf("bns") === 0 ? "Name"
           : root.tx && (root.tx.kind === "stake" || root.tx.kind === "unlock") ? "Master node"
           : root.tx && root.tx.kind === "token-update" ? "New owner" : "To"
        value: root.tx ? root.tx.destinations.join("\n") : ""
        valueColor: Theme.green
        visible: root.tx && root.tx.destinations.length > 0
    }
    KeyValue { key: "Speed"; value: "FLASH · instant confirmation, higher fee"; valueColor: Theme.amber; visible: root.tx && root.tx.flash }
    KeyValue { key: "Transactions"; value: root.tx ? String(root.tx.txCount) : ""; visible: root.tx && root.tx.txCount > 1 }
    Mono {
        text: root.tx ? root.tx.note : ""
        visible: text.length > 0
        color: Theme.textDim
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.topMargin: 4
    }

    footer: [
        Mono {
            text: WalletManager.wallet && WalletManager.wallet.busy ? WalletManager.wallet.busyText : ""
            color: Theme.textDim
            font.pixelSize: 11
            Layout.fillWidth: true
        },
        BdxButton { text: "Cancel"; onClicked: root.close(); enabled: !root.committing },
        BdxButton {
            // Name the action being confirmed, not always "Send".
            text: !root.tx ? "Send" : ({
                "sweep": "Sweep", "stake": "Stake", "unlock": "Request unlock", "bns-buy": "Buy name",
                "bns-renew": "Renew", "bns-update": "Update", "token-register": "Register",
                "token-mint": "Mint", "token-burn": "Burn", "token-update": "Update"
            }[root.tx.kind] || "Send")
            variant: "primary"
            enabled: !root.committing
            onClicked: {
                root.committing = true
                WalletManager.wallet.commitTransaction(root.tx)
            }
        }
    ]

    Connections {
        target: WalletManager.wallet
        function onTransactionCommitted(success, message, txids) {
            if (!root.opened)
                return
            root.close()
        }
    }
}
