import QtQuick
import QtQuick.Layouts
import BeldexGui

BdxPage {
    id: page
    readonly property var w: WalletManager.wallet
    property var secrets: ({})

    title: "Settings"
    subtitle: w ? WalletManager.prettyPath(w.path) : ""

    RowLayout {
        Layout.fillWidth: true
        spacing: 22

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 22

            Panel {
                title: "Node"
                note: App.usingLocalNode ? "local node" : page.w.connected ? "connected" : "disconnected"
                Layout.fillWidth: true

                readonly property int net: page.w.netType
                readonly property string mode: App.nodeModeFor(net)

                Mono {
                    text: "The wallet reads the blockchain through a Beldex node. Auto uses a public node when one answers on "
                        + page.w.netName.toLowerCase() + ", otherwise it runs beldexd on this computer. A local node gives the best privacy."
                    color: Theme.textDim
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                ChipGroup {
                    current: App.nodeModeFor(page.w.netType)
                    model: [{ value: "auto", label: "Auto" }, { value: "remote", label: "Remote node" }, { value: "local", label: "Local node" }]
                    onPicked: v => App.setNodeModeFor(page.w.netType, v)
                }

                // ---- remote --------------------------------------------------------------------
                ColumnLayout {
                    visible: App.nodeModeFor(page.w.netType) !== "local"
                    spacing: 12
                    Layout.fillWidth: true
                    ColumnLayout {
                        visible: App.publicNodesFor(page.w.netType).length > 0
                        spacing: 8
                        Layout.fillWidth: true
                        Caption { text: "Public remote nodes" }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: App.publicNodesFor(page.w.netType)
                                BdxButton {
                                    required property string modelData
                                    compact: true
                                    text: modelData.split(".")[0]
                                    variant: nodeField.text === modelData ? "accent" : "ghost"
                                    onClicked: nodeField.text = modelData
                                }
                            }
                        }
                    }
                    Mono {
                        visible: App.publicNodesFor(page.w.netType).length === 0
                        text: "There is no public " + page.w.netName.toLowerCase() + " node. Enter one you know, or use a local node."
                        color: Theme.amber
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    BdxTextField {
                        id: nodeField
                        label: "Node address (host:port)"
                        text: App.nodeFor(page.w.netType)
                        placeholderText: page.w.netType === 1 ? "host:29091" : "host:19091"
                    }
                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true
                        BdxTextField { id: nodeUser; label: "RPC login (optional)"; text: App.settings.nodeUser; Layout.fillWidth: true }
                        BdxTextField { id: nodePass; label: "RPC password"; text: App.settings.nodePassword; echoMode: TextInput.Password; Layout.fillWidth: true }
                    }
                    BdxSwitch {
                        id: trusted
                        text: "Trusted node"
                        description: "Only enable for a node you run yourself: the wallet then trusts it for some privacy-sensitive requests."
                        checked: App.settings.nodeTrusted
                        Layout.fillWidth: true
                    }
                }

                // ---- local ---------------------------------------------------------------------
                ColumnLayout {
                    visible: App.nodeModeFor(page.w.netType) !== "remote"
                    spacing: 12
                    Layout.fillWidth: true
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: DaemonManager.running ? (DaemonManager.synchronized ? Theme.green : Theme.amber)
                                 : DaemonManager.starting ? Theme.amber : Theme.textFaint
                        }
                        Mono {
                            text: DaemonManager.statusText + (DaemonManager.running && !DaemonManager.ownsProcess ? " (started outside the app)" : "")
                            color: Theme.text
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        BdxButton {
                            compact: true
                            text: DaemonManager.running || DaemonManager.starting ? "Stop" : "Start"
                            variant: DaemonManager.running || DaemonManager.starting ? "danger" : "accent"
                            enabled: DaemonManager.ownsProcess || !DaemonManager.running
                            onClicked: {
                                if (DaemonManager.running || DaemonManager.starting) DaemonManager.stop()
                                else App.startLocalNode(page.w.netType)
                            }
                        }
                    }
                    Rectangle {
                        visible: DaemonManager.running && !DaemonManager.synchronized
                        Layout.fillWidth: true
                        implicitHeight: 3
                        color: Theme.border
                        Rectangle { height: parent.height; width: parent.width * DaemonManager.syncProgress; color: Theme.amber }
                    }
                    Mono {
                        visible: DaemonManager.lastError.length > 0
                        text: DaemonManager.lastError
                        color: Theme.red
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    BdxTextField {
                        id: daemonPath
                        label: "beldexd program"
                        text: App.settings.daemonPath.length ? App.settings.daemonPath : DaemonManager.binaryPath
                        placeholderText: "path to beldexd"
                        monoSmall: true
                        error: !DaemonManager.binaryFound && text.length === 0 ? "beldexd not found — run scripts/build-beldex-core.sh" : ""
                    }
                    BdxTextField {
                        id: dataDir
                        label: "Blockchain folder"
                        text: App.settings.daemonDataDir
                        placeholderText: WalletManager.prettyPath(DaemonManager.defaultDataDir(page.w.netType))
                        monoSmall: true
                        help: page.w.netType === 0 ? "Mainnet needs about 45 GB (less when pruned) and several hours for the first sync."
                                                   : "Testnet is small; the first sync takes minutes to an hour."
                    }
                    BdxTextField {
                        id: peers
                        visible: page.w.netType === 1
                        label: "Testnet peers (host:port, comma separated)"
                        text: App.settings.testnetPeers
                        placeholderText: "1.2.3.4:29090"
                        monoSmall: true
                        help: "Used in addition to the built-in testnet seed nodes."
                    }
                    BdxSwitch {
                        id: prune
                        text: "Prune blockchain"
                        description: "Keeps about a third of the chain data. Your wallet works the same."
                        checked: App.settings.daemonPrune
                        Layout.fillWidth: true
                    }
                    Mono {
                        text: "Log: " + WalletManager.prettyPath(DaemonManager.logPath)
                        color: Theme.textFaint
                        font.pixelSize: 10
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    Item { Layout.fillWidth: true }
                    BdxButton {
                        text: "Save & connect"
                        variant: "primary"
                        onClicked: {
                            const net = page.w.netType
                            if (App.nodeModeFor(net) !== "local") {
                                App.setNodeFor(net, nodeField.text.trim())
                                App.settings.nodeUser = nodeUser.text
                                App.settings.nodePassword = nodePass.text
                                App.settings.nodeTrusted = trusted.checked
                            }
                            if (App.nodeModeFor(net) !== "remote") {
                                App.settings.daemonPath = daemonPath.text.trim() === DaemonManager.binaryPath ? "" : daemonPath.text.trim()
                                App.settings.daemonDataDir = dataDir.text.trim()
                                App.settings.testnetPeers = peers.text.trim()
                                App.settings.daemonPrune = prune.checked
                            }
                            App.connectWallet()
                        }
                    }
                }
            }

            Panel {
                title: "Sign / verify message"
                Layout.fillWidth: true
                BdxTextArea { id: msg; label: "Message"; minHeight: 70 }
                RowLayout {
                    spacing: 10
                    BdxButton {
                        text: "Sign with this wallet"
                        variant: "accent"
                        enabled: msg.text.length > 0
                        onClicked: { sig.text = page.w.signMessage(msg.text); signer.text = page.w.primaryAddress }
                    }
                    Item { Layout.fillWidth: true }
                }
                BdxTextField { id: signer; label: "Signer address"; monoSmall: true }
                BdxTextField { id: sig; label: "Signature"; monoSmall: true }
                RowLayout {
                    Item { Layout.fillWidth: true }
                    BdxButton {
                        text: "Verify"
                        enabled: msg.text.length > 0 && sig.text.length > 0 && signer.text.length > 0
                        onClicked: {
                            const ok = page.w.verifyMessage(msg.text, signer.text, sig.text)
                            App.toast(ok ? "Signature is valid" : "Signature is NOT valid for this address", ok ? "ok" : "error")
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 460
            Layout.alignment: Qt.AlignTop
            spacing: 22

            Panel {
                title: "Wallet"
                Layout.fillWidth: true
                KeyValue { key: "Name"; keyWidth: 120; value: page.w.name }
                KeyValue { key: "Network"; keyWidth: 120; value: page.w.netName }
                KeyValue { key: "Restore height"; keyWidth: 120; value: App.fmtHeight(page.w.restoreHeight) }
                KeyValue { key: "Type"; keyWidth: 120; value: page.w.watchOnly ? "view-only" : "full (can spend)" }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    BdxButton {
                        text: "Show seed & keys"
                        variant: "accent"
                        onClicked: App.askPassword("Your seed gives full control of your funds. Make sure nobody can see your screen.",
                                                   pw => { page.secrets = page.w.secrets(pw) })
                    }
                    BdxButton { text: "Change password"; onClicked: pwDialog.open() }
                    BdxButton { text: "Rescan…"; onClicked: rescanDialog.open() }
                    BdxButton { text: "Close wallet"; variant: "danger"; onClicked: WalletManager.closeWallet() }
                }

                ColumnLayout {
                    visible: page.secrets.seed !== undefined
                    spacing: 12
                    Layout.fillWidth: true
                    CopyField { label: "Mnemonic seed (" + (page.secrets.seedLanguage || "") + ")"; value: page.secrets.seed || ""; secret: true; valueColor: Theme.amber }
                    CopyField { label: "Secret view key"; value: page.secrets.secretViewKey || ""; secret: true }
                    CopyField { label: "Public view key"; value: page.secrets.publicViewKey || "" }
                    CopyField { label: "Secret spend key"; value: page.secrets.secretSpendKey || ""; secret: true; valueColor: Theme.amber }
                    CopyField { label: "Public spend key"; value: page.secrets.publicSpendKey || "" }
                    BdxButton { text: "Hide secrets"; onClicked: page.secrets = ({}) }
                }
            }

            Panel {
                title: "About"
                Layout.fillWidth: true
                KeyValue { key: "Version"; keyWidth: 120; value: "beldex-gui " + WalletManager.version }
                KeyValue { key: "Wallet engine"; keyWidth: 120; value: WalletManager.engineName; valueColor: WalletManager.isDemo ? Theme.amber : Theme.text }
                KeyValue { key: "Log file"; keyWidth: 120; value: WalletManager.prettyPath(WalletManager.logPath); visible: WalletManager.logPath.length > 0; valueColor: Theme.textDim }
                KeyValue { key: "Toolkit"; keyWidth: 120; value: "Qt 6 / QML · native C++" }
                RowLayout {
                    spacing: 8
                    BdxButton { compact: true; text: "beldex.io"; onClicked: Qt.openUrlExternally("https://beldex.io") }
                    BdxButton { compact: true; text: "Explorer"; onClicked: Qt.openUrlExternally(App.explorerBase(page.w.netType)) }
                }
            }
        }
    }

    BdxDialog {
        id: rescanDialog
        title: "Rescan blockchain"
        dialogWidth: 520
        onOpened: { rescanHeight.text = String(page.w.restoreHeight); rescanDate.text = "" }
        Mono {
            text: "Forgets what this wallet has scanned and scans again from the block below. Use it if your balance "
                + "looks wrong, or if a restored wallet skipped its history. Block 0 scans the whole chain (about an hour)."
            color: Theme.textDim
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        RowLayout {
            spacing: 12
            Layout.fillWidth: true
            BdxTextField {
                id: rescanHeight
                label: "Start at block"
                validator: IntValidator { bottom: 0 }
                Layout.fillWidth: true
            }
            BdxTextField {
                id: rescanDate
                label: "…or date (YYYY-MM-DD)"
                placeholderText: "2024-01-31"
                Layout.preferredWidth: 220
                field.onTextEdited: {
                    if (/^\d{4}-\d{2}-\d{2}$/.test(field.text))
                        rescanHeight.text = String(WalletManager.estimateHeightForDate(page.w.netType, field.text))
                }
            }
        }
        footer: [
            BdxButton { text: "From block 0"; onClicked: rescanHeight.text = "0" },
            Item { Layout.fillWidth: true },
            BdxButton { text: "Cancel"; onClicked: rescanDialog.close() },
            BdxButton {
                text: "Rescan"
                variant: "primary"
                enabled: rescanHeight.text.length > 0
                onClicked: { page.w.rescanBlockchainFrom(Number(rescanHeight.text)); rescanDialog.close(); App.go("overview") }
            }
        ]
    }

    BdxDialog {
        id: pwDialog
        title: "Change password"
        dialogWidth: 460
        onOpened: { oldPw.text = ""; newPw.text = ""; newPw2.text = "" }
        BdxTextField { id: oldPw; label: "Current password"; echoMode: TextInput.Password }
        BdxTextField { id: newPw; label: "New password"; echoMode: TextInput.Password }
        BdxTextField {
            id: newPw2
            label: "Repeat new password"
            echoMode: TextInput.Password
            error: text.length > 0 && text !== newPw.text ? "Passwords don't match" : ""
        }
        footer: [
            Item { Layout.fillWidth: true },
            BdxButton { text: "Cancel"; onClicked: pwDialog.close() },
            BdxButton {
                text: "Change"
                variant: "primary"
                enabled: newPw.text.length > 0 && newPw.text === newPw2.text
                onClicked: if (page.w.changePassword(oldPw.text, newPw.text)) pwDialog.close()
            }
        ]
    }

    onVisibleChanged: if (!visible) secrets = ({})
}
