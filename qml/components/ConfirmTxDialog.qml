import QtQuick
import QtQuick.Layouts
import BeldexGui

// Shows a PendingTransaction and commits it on confirm.
BdxDialog {
    id: root
    property var tx: null
    property bool committing: false

    title: !tx ? "" : {
        "transfer": "Confirm transaction", "sweep": "Confirm sweep", "stake": "Confirm stake",
        "unlock": "Request stake unlock", "bns-buy": "Confirm BNS purchase",
        "bns-renew": "Confirm BNS renewal", "bns-update": "Confirm BNS update"
    }[tx.kind] || "Confirm"

    function show(t) { tx = t; committing = false; open() }

    onClosed: {
        if (tx && !committing && WalletManager.wallet)
            WalletManager.wallet.discardTransaction(tx)
        tx = null
    }

    KeyValue {
        key: root.tx && root.tx.kind === "stake" ? "Stake amount" : "Amount"
        value: root.tx ? root.tx.amount + " BDX" : ""
        visible: root.tx && root.tx.kind !== "unlock" && root.tx.kind !== "bns-update"
    }
    KeyValue { key: "Network fee"; value: root.tx ? root.tx.fee + " BDX" : "" }
    KeyValue {
        key: "Total"
        value: root.tx ? root.tx.total + " BDX" : ""
        valueColor: Theme.green
        visible: root.tx && root.tx.kind !== "unlock" && root.tx.kind !== "bns-update"
    }
    KeyValue {
        key: root.tx && root.tx.kind.indexOf("bns") === 0 ? "Name"
           : root.tx && (root.tx.kind === "stake" || root.tx.kind === "unlock") ? "Master node" : "To"
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
            text: root.tx && root.tx.kind === "unlock" ? "Request unlock" : "Send"
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
