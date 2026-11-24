import QtQuick
import QtQuick.Layouts
import BeldexGui

// Asks for the wallet password; calls `onAccepted(password)` callback if it verifies.
BdxDialog {
    id: root
    property string message: "Enter your wallet password to continue."
    property var callback: null
    title: "Wallet password"
    dialogWidth: 460

    function ask(msg, cb) {
        message = msg
        callback = cb
        pw.text = ""
        pw.error = ""
        open()
        pw.field.forceActiveFocus()
    }
    function submit() {
        if (!WalletManager.wallet.checkPassword(pw.text)) {
            pw.error = "Wrong password"
            return
        }
        const cb = callback
        const p = pw.text
        pw.text = ""
        close()
        if (cb) cb(p)
    }

    Mono { text: root.message; color: Theme.textDim; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    BdxTextField { id: pw; label: "Password"; echoMode: TextInput.Password; onAccepted: root.submit() }

    footer: [
        Item { Layout.fillWidth: true },
        BdxButton { text: "Cancel"; onClicked: root.close() },
        BdxButton { text: "Unlock"; variant: "primary"; onClicked: root.submit() }
    ]
}
