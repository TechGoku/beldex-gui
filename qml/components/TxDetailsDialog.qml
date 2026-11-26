import QtQuick
import QtQuick.Layouts
import BeldexGui

BdxDialog {
    id: root
    property var tx: ({})
    property string txKey: ""
    title: "Transaction details"
    dialogWidth: 720

    function show(t) {
        tx = t
        txKey = ""
        note.text = t.note || ""
        open()
    }

    RowLayout {
        spacing: 10
        KindBadge { kind: root.tx.kind || "in"; pending: !!root.tx.pending }
        Mono {
            text: root.tx.amountFull === undefined ? ""
                : root.tx.kind === "token-update" ? ""
                : (root.tx.incoming || root.tx.kind === "token-register" || root.tx.kind === "token-mint" ? "+" : "−")
                  + root.tx.amountFull + " " + (root.tx.unit || "BDX")
            color: root.tx.incoming ? Theme.green : Theme.text
            font.pixelSize: 18
            font.bold: true
        }
    }
    CopyField { label: "Transaction ID"; value: root.tx.hash || "" }
    CopyField { label: "Token ID"; value: root.tx.tokenId || ""; visible: !!root.tx.tokenId }
    KeyValue { key: "Date"; value: root.tx.dateText || "" }
    KeyValue { key: "Block height"; value: root.tx.pending ? "in mempool" : App.fmtHeight(root.tx.height || 0) }
    KeyValue { key: "Confirmations"; value: String(root.tx.confirmations || 0); visible: !root.tx.pending }
    KeyValue { key: "Fee"; value: (root.tx.fee || "0") + " BDX"; visible: !root.tx.incoming }
    KeyValue { key: "Payment ID"; value: root.tx.paymentId || ""; visible: !!root.tx.paymentId && !/^0+$/.test(root.tx.paymentId) }
    KeyValue {
        key: "Destinations"
        value: (root.tx.destinations || []).join("\n")
        valueColor: Theme.green
        visible: (root.tx.destinations || []).length > 0
    }
    CopyField { label: "Transaction key (proof of payment)"; value: root.txKey; visible: root.txKey.length > 0; secret: true }

    BdxTextField {
        id: note
        label: "Note (stored only in this wallet)"
        placeholderText: "Add a note…"
        trailing: BdxButton {
            compact: true
            text: "Save"
            onClicked: { WalletManager.wallet.setTxNote(root.tx.hash, note.text); App.toast("Note saved", "ok") }
        }
    }

    footer: [
        BdxButton { text: "View on explorer"; variant: "accent"; onClicked: App.openTx(root.tx.hash) },
        BdxButton {
            text: "Get tx key"
            visible: !root.tx.incoming
            onClicked: {
                const k = WalletManager.wallet.txKey(root.tx.hash)
                root.txKey = k
                if (!k.length) App.toast("No transaction key stored for this transaction", "info")
            }
        },
        Item { Layout.fillWidth: true },
        BdxButton { text: "Close"; onClicked: root.close() }
    ]
}
