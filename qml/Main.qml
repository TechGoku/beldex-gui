import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BeldexGui

ApplicationWindow {
    id: window
    width: 1320
    height: 880
    minimumWidth: 1040
    minimumHeight: 700
    visible: true
    color: Theme.bg
    title: WalletManager.walletOpen ? "Beldex Wallet — " + WalletManager.wallet.name : "Beldex Wallet"

    readonly property bool showWizard: !WalletManager.walletOpen || App.wizardHold

    DotBackground { anchors.fill: parent }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Demo builds must never be mistaken for a real wallet.
        Rectangle {
            visible: WalletManager.isDemo
            Layout.fillWidth: true
            implicitHeight: 28
            color: "#1d1400"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.amber }
            Mono {
                anchors.centerIn: parent
                text: "DEMO BUILD · simulated wallet, no connection to the Beldex network · do not use real seeds"
                color: Theme.amber
                font.pixelSize: 11
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: window.showWizard ? wizardComponent : shellComponent
        }
    }

    Component { id: wizardComponent; Wizard {} }
    Component { id: shellComponent; WalletShell {} }

    // --- global services ---------------------------------------------------------------------
    Toasts { id: toasts; anchors.fill: parent; z: 100 }
    ConfirmTxDialog { id: confirmDialog }
    PasswordDialog { id: passwordDialog }
    TxDetailsDialog { id: txDetails }
    NetworkSwitchDialog { id: networkDialog }

    Connections {
        target: App
        function onToastRequested(text, tone) { toasts.show(text, tone) }
        function onPasswordRequested(message, callback) { passwordDialog.ask(message, callback) }
        function onTxDetailsRequested(tx) { txDetails.show(tx) }
        function onNetworkSwitchRequested() { networkDialog.show() }
    }

    Connections {
        target: WalletManager
        function onWalletError(message) { toasts.show(message, "error") }
        // Dialogs may hold objects owned by the wallet that was just closed.
        function onWalletClosed() {
            App.nodeState = ""
            confirmDialog.tx = null
            confirmDialog.close()
            txDetails.close()
            passwordDialog.close()
            networkDialog.close()
        }
        function onWalletOpened() {
            App.settings.lastWalletPath = WalletManager.wallet.path
            App.page = "overview"
            App.connectWallet()
        }
    }

    Connections {
        target: WalletManager.wallet
        ignoreUnknownSignals: true
        function onError(message) { toasts.show(message, "error") }
        function onInfo(message) { toasts.show(message, "info") }
        function onTransactionCreated(tx) { confirmDialog.show(tx) }
        function onTransactionCommitted(success, message, txids) {
            toasts.show(success ? message + (txids.length ? " · " + txids[0].slice(0, 12) + "…" : "") : message,
                        success ? "ok" : "error")
        }
        function onMoneyReceived(txid, amount) { toasts.show("Received " + amount + " BDX", "ok") }
    }

    // Quitting saves the wallet and stops a local node we started. Both take seconds, so do them in the
    // background behind a "shutting down" screen instead of freezing the window, then quit.
    property bool quitting: false
    onClosing: close => {
        if (quitting)
            return                      // second close request: let it go
        if (!WalletManager.walletOpen && !WalletManager.closing && !DaemonManager.ownsProcess)
            return
        close.accepted = false
        quitting = true
        WalletManager.closeWallet()
        if (DaemonManager.ownsProcess)
            DaemonManager.stop()
        quitWatch.start()
    }
    Timer {
        id: quitWatch
        interval: 150
        repeat: true
        onTriggered: if (!WalletManager.walletOpen && !WalletManager.closing && !DaemonManager.ownsProcess) Qt.quit()
    }
    Rectangle {
        anchors.fill: parent
        visible: window.quitting
        z: 200
        color: Qt.rgba(0.02, 0.02, 0.02, 0.94)
        MouseArea { anchors.fill: parent }   // swallow input while shutting down
        Column {
            anchors.centerIn: parent
            spacing: 14
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "qrc:/qt/qml/BeldexGui/images/logo-icon.png"
                sourceSize.height: 56
                RotationAnimator on rotation { running: window.quitting; from: 0; to: 360; duration: 2400; loops: Animation.Infinite }
            }
            Mono {
                anchors.horizontalCenter: parent.horizontalCenter
                text: WalletManager.closing || WalletManager.walletOpen ? "Saving wallet…"
                    : DaemonManager.ownsProcess ? "Stopping local node…" : "Closing…"
                color: Theme.green
                font.pixelSize: 14
            }
        }
    }
}
