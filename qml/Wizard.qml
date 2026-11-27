import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import BeldexGui

// First-run / no-wallet flow: create, restore (seed or keys), open. After "create" it keeps
// the wallet behind the seed backup + verification steps before showing the wallet.
Item {
    id: wiz
    objectName: "wizard"  // the screenshot tour opens its steps
    property string step: App.wizardHold && WalletManager.newWalletSeed.length > 0 ? "seed" : "home"
    // Mainnet only unless test networks are enabled (WalletManager.showTestNetworks).
    property int net: WalletManager.showTestNetworks ? App.settings.netType : 0
    property var wallets: listWallets()
    property string openPath: ""
    property var verifyIdx: []

    readonly property var seedWords: WalletManager.newWalletSeed.split(" ").filter(s => s.length > 0)

    // The wallets folder, plus the last wallet used when it lives somewhere else (opened with Browse…).
    function listWallets() {
        const list = WalletManager.findWallets(App.walletDir)
        const last = App.settings.lastWalletPath
        if (last.length > 0 && !list.some(w => w.path === last) && WalletManager.walletExists(last))
            list.unshift({ name: last.split(/[\\/]/).pop(), path: last, modified: "last used" })
        return list
    }
    function refreshWallets() { wallets = listWallets() }
    function pickVerifyWords() {
        const idx = []
        while (idx.length < 3) {
            const i = Math.floor(Math.random() * seedWords.length)
            if (idx.indexOf(i) < 0) idx.push(i)
        }
        verifyIdx = idx.sort((a, b) => a - b)
    }
    function finish() {
        WalletManager.clearNewWalletSeed()
        App.wizardHold = false
    }
    function nameError(name) {
        if (name.length === 0) return ""
        if (!/^[A-Za-z0-9 _.-]+$/.test(name)) return "Use letters, numbers, space, dot, dash or underscore"
        if (WalletManager.walletExists(WalletManager.walletPath(App.walletDir, name))) return "A wallet with this name already exists"
        return ""
    }

    Connections {
        target: WalletManager
        function onWalletError(message) { App.wizardHold = false }
        function onWalletOpened() { if (App.wizardHold) wiz.step = "seed" }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 120
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: col
            width: Math.min(parent.width - Theme.gutter * 2, 1040)
            x: (parent.width - width) / 2
            y: 56
            spacing: 26

            RowLayout {
                Layout.fillWidth: true
                Image {
                    source: "qrc:/qt/qml/BeldexGui/images/logo-wordmark.png"
                    sourceSize.height: 30
                    Layout.preferredHeight: 30
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                }
                Item { Layout.fillWidth: true }
                ChipGroup {
                    visible: wiz.step === "home" && WalletManager.showTestNetworks
                    current: wiz.net
                    model: [{ value: 0, label: "Mainnet" }, { value: 1, label: "Testnet" }, { value: 2, label: "Devnet" }]
                    onPicked: v => { wiz.net = v; App.settings.netType = v }
                }
            }

            PageTitle {
                Layout.fillWidth: true
                title: ({ home: "Beldex wallet", create: "Create wallet", seed: "Your recovery seed",
                          verify: "Verify your seed", restoreSeed: "Restore from seed",
                          restoreKeys: "Restore from keys" })[wiz.step] || ""
                subtitle: wiz.step === "home"
                          ? "Private, untraceable BDX. Wallets are stored in " + WalletManager.prettyPath(App.walletDir)
                          : App.netNames[wiz.net] + " · " + WalletManager.prettyPath(App.walletDir)
            }

            // ---------------------------------------------------------------- home
            ColumnLayout {
                visible: wiz.step === "home"
                spacing: 26
                Layout.fillWidth: true

                StatGrid {
                    Layout.fillWidth: true
                    columns: 3
                    Repeater {
                        model: [
                            { step: "create", label: "New", title: "Create wallet", text: "Generate a new wallet and a 25-word recovery seed." },
                            { step: "restoreSeed", label: "Restore", title: "From seed", text: "Recover a wallet from its 25-word mnemonic seed." },
                            { step: "restoreKeys", label: "Restore", title: "From keys", text: "Recover from address + private keys, or make a view-only wallet." }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 100
                            implicitHeight: 150
                            color: optMouse.containsMouse ? "#111" : Theme.panel
                            Rectangle { width: parent.width; height: 2; color: Theme.green; visible: optMouse.containsMouse }
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 20
                                spacing: 10
                                Caption { text: modelData.label }
                                Text {
                                    text: modelData.title
                                    color: optMouse.containsMouse ? Theme.green : Theme.text
                                    font.family: Theme.display
                                    font.pixelSize: 16
                                    font.letterSpacing: 1.2
                                    font.capitalization: Font.AllUppercase
                                    renderType: Text.NativeRendering
                                }
                                Mono { text: modelData.text; color: Theme.textDim; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                                Item { Layout.fillHeight: true }
                            }
                            MouseArea {
                                id: optMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: wiz.step = modelData.step
                            }
                        }
                    }
                }

                Panel {
                    title: "Open a wallet"
                    note: wiz.wallets.length + " found"
                    contentPadding: 0
                    spacing: 0
                    Layout.fillWidth: true
                    headerExtra: [
                        BdxButton { compact: true; text: "Browse…"; onClicked: fileDialog.open() },
                        BdxButton { compact: true; text: "Folder…"; onClicked: folderDialog.open() }
                    ]

                    Repeater {
                        model: wiz.wallets
                        delegate: Rectangle {
                            id: wrow
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 52
                            color: rowMouse.containsMouse ? Theme.rowHover : "transparent"
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { wiz.openPath = wrow.modelData.path; openDialog.open() }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.pad
                                anchors.rightMargin: Theme.pad
                                spacing: 14
                                Mono { text: wrow.modelData.name; color: Theme.green; font.bold: true; Layout.preferredWidth: 220; elide: Text.ElideRight }
                                Mono { text: WalletManager.prettyPath(wrow.modelData.path); color: Theme.textFaint; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 11 }
                                Mono { text: wrow.modelData.modified; color: Theme.textDim; font.pixelSize: 11 }
                                BdxButton { compact: true; text: "Open"; variant: "accent"; onClicked: { wiz.openPath = wrow.modelData.path; openDialog.open() } }
                            }
                        }
                    }
                    Mono {
                        visible: wiz.wallets.length === 0
                        text: "No wallets in this folder yet."
                        color: Theme.textFaint
                        font.italic: true
                        Layout.margins: Theme.pad
                    }
                }
            }

            // ---------------------------------------------------------------- create
            Panel {
                visible: wiz.step === "create"
                title: "Name & password"
                Layout.fillWidth: true
                Layout.maximumWidth: 620

                BdxTextField { id: cName; label: "Wallet name"; placeholderText: "my-beldex-wallet"; error: wiz.nameError(text) }
                BdxTextField {
                    id: cPw
                    label: "Password"
                    echoMode: TextInput.Password
                    help: text.length === 0 ? "Encrypts the wallet file on this computer. It cannot be recovered — but your seed can restore the wallet."
                        : text.length < 8 ? "Weak — use at least 8 characters" : text.length < 14 ? "OK" : "Strong"
                }
                BdxTextField { id: cPw2; label: "Repeat password"; echoMode: TextInput.Password; error: text.length > 0 && text !== cPw.text ? "Passwords don't match" : "" }
                RowLayout {
                    BdxButton { text: "Back"; onClicked: wiz.step = "home" }
                    Item { Layout.fillWidth: true }
                    BdxButton {
                        text: WalletManager.busy ? WalletManager.busyText : "Create wallet"
                        variant: "primary"
                        enabled: cName.text.length > 0 && wiz.nameError(cName.text) === "" && cPw.text === cPw2.text && !WalletManager.busy
                        onClicked: {
                            App.wizardHold = true
                            WalletManager.createWallet(App.walletDir, cName.text.trim(), cPw.text, wiz.net)
                        }
                    }
                }
            }

            // ---------------------------------------------------------------- seed
            Panel {
                visible: wiz.step === "seed"
                title: "Write these 25 words down, in order"
                note: "offline · on paper"
                Layout.fillWidth: true

                Mono {
                    text: "Anyone with this seed can spend your BDX. Never type it into a website or share it — Beldex support will never ask for it."
                    color: Theme.amber
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                GridLayout {
                    columns: 5
                    columnSpacing: 1
                    rowSpacing: 1
                    Layout.fillWidth: true
                    Repeater {
                        model: wiz.seedWords
                        delegate: Rectangle {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            implicitHeight: 44
                            color: "#080808"
                            border.color: Theme.border
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 14
                                spacing: 10
                                Mono { text: String(index + 1).padStart(2, "0"); color: Theme.textFaint }
                                Mono { text: modelData; color: Theme.text; font.bold: true; font.pixelSize: 13 }
                            }
                        }
                    }
                }
                RowLayout {
                    BdxButton { text: "Copy seed"; onClicked: { WalletManager.copyToClipboard(WalletManager.newWalletSeed); App.toast("Seed copied — clear your clipboard after saving it", "info") } }
                    Item { Layout.fillWidth: true }
                    BdxButton {
                        text: "I've written it down"
                        variant: "primary"
                        onClicked: { wiz.pickVerifyWords(); v0.text = ""; v1.text = ""; v2.text = ""; wiz.step = "verify" }
                    }
                }
            }

            // ---------------------------------------------------------------- verify
            Panel {
                id: verifyPanel
                visible: wiz.step === "verify"
                title: "Enter the requested words"
                Layout.fillWidth: true
                Layout.maximumWidth: 620

                readonly property bool allOk: wiz.verifyIdx.length === 3
                    && v0.text.trim().toLowerCase() === wiz.seedWords[wiz.verifyIdx[0]]
                    && v1.text.trim().toLowerCase() === wiz.seedWords[wiz.verifyIdx[1]]
                    && v2.text.trim().toLowerCase() === wiz.seedWords[wiz.verifyIdx[2]]

                BdxTextField { id: v0; label: "Word #" + (wiz.verifyIdx[0] + 1) }
                BdxTextField { id: v1; label: "Word #" + (wiz.verifyIdx[1] + 1) }
                BdxTextField { id: v2; label: "Word #" + (wiz.verifyIdx[2] + 1) }
                RowLayout {
                    BdxButton { text: "Show seed again"; onClicked: wiz.step = "seed" }
                    Item { Layout.fillWidth: true }
                    BdxButton { text: "Open wallet"; variant: "primary"; enabled: verifyPanel.allOk; onClicked: wiz.finish() }
                }
            }

            // ---------------------------------------------------------------- restore from seed
            Panel {
                visible: wiz.step === "restoreSeed"
                title: "Seed & restore height"
                Layout.fillWidth: true
                Layout.maximumWidth: 760

                BdxTextArea {
                    id: rSeed
                    label: "25-word mnemonic seed"
                    placeholderText: "word1 word2 word3 …"
                    readonly property int words: text.trim().split(/\s+/).filter(s => s.length).length
                    help: words + " / 25 words"
                    error: words > 25 ? "Too many words" : ""
                }
                BdxTextField { id: rOffset; label: "Seed offset passphrase (optional)"; echoMode: TextInput.Password; help: "Only if you created the wallet with a seed offset." }
                RowLayout {
                    spacing: 12
                    BdxTextField {
                        id: rHeight
                        label: "Restore height"
                        placeholderText: "0"
                        validator: IntValidator { bottom: 0 }
                        help: Number(text || 0) > 0 ? "Block height or date of your first transaction. Speeds up sync a lot."
                              : "Empty scans the whole chain, which can take an hour or more on mainnet. A date before your first transaction is much faster."
                        Layout.fillWidth: true
                    }
                    BdxTextField {
                        id: rDate
                        label: "Or date"
                        placeholderText: "YYYY-MM-DD"
                        Layout.preferredWidth: 230
                        Layout.alignment: Qt.AlignTop
                        field.onTextEdited: {
                            if (/^\d{4}-\d{2}-\d{2}$/.test(field.text))
                                rHeight.text = String(WalletManager.estimateHeightForDate(wiz.net, field.text))
                        }
                    }
                }
                BdxTextField { id: rName; label: "Wallet name"; error: wiz.nameError(text) }
                RowLayout {
                    spacing: 12
                    BdxTextField { id: rPw; label: "New password"; echoMode: TextInput.Password; Layout.fillWidth: true }
                    BdxTextField { id: rPw2; label: "Repeat"; echoMode: TextInput.Password; Layout.fillWidth: true; error: text.length > 0 && text !== rPw.text ? "Passwords don't match" : "" }
                }
                RowLayout {
                    BdxButton { text: "Back"; onClicked: wiz.step = "home" }
                    Item { Layout.fillWidth: true }
                    BdxButton {
                        text: WalletManager.busy ? WalletManager.busyText : "Restore wallet"
                        variant: "primary"
                        enabled: rSeed.words === 25 && rName.text.length > 0 && wiz.nameError(rName.text) === "" && rPw.text === rPw2.text && !WalletManager.busy
                        onClicked: WalletManager.restoreFromSeed(App.walletDir, rName.text.trim(), rPw.text, rSeed.text.trim().toLowerCase(),
                                                                 wiz.net, Number(rHeight.text || 0), rOffset.text)
                    }
                }
            }

            // ---------------------------------------------------------------- restore from keys
            Panel {
                visible: wiz.step === "restoreKeys"
                title: "Address & keys"
                Layout.fillWidth: true
                Layout.maximumWidth: 760

                BdxTextField {
                    id: kAddr
                    label: "Primary address"
                    monoSmall: true
                    error: text.length > 0 && !WalletManager.addressValid(text, wiz.net) ? "Not a valid " + App.netNames[wiz.net].toLowerCase() + " address" : ""
                }
                BdxTextField { id: kView; label: "Secret view key"; monoSmall: true; error: text.length > 0 && !/^[0-9a-fA-F]{64}$/.test(text) ? "64 hex characters" : "" }
                BdxTextField {
                    id: kSpend
                    label: "Secret spend key (leave empty for a view-only wallet)"
                    monoSmall: true
                    error: text.length > 0 && !/^[0-9a-fA-F]{64}$/.test(text) ? "64 hex characters" : ""
                }
                RowLayout {
                    spacing: 12
                    BdxTextField {
                        id: kHeight
                        label: "Restore height"
                        placeholderText: "0"
                        validator: IntValidator { bottom: 0 }
                        help: Number(text || 0) > 0 ? "Block height or date of your first transaction. Speeds up sync a lot."
                              : "Empty scans the whole chain, which can take an hour or more on mainnet. A date before your first transaction is much faster."
                        Layout.fillWidth: true
                    }
                    BdxTextField {
                        label: "Or date"
                        placeholderText: "YYYY-MM-DD"
                        Layout.preferredWidth: 230
                        Layout.alignment: Qt.AlignTop
                        field.onTextEdited: {
                            if (/^\d{4}-\d{2}-\d{2}$/.test(field.text))
                                kHeight.text = String(WalletManager.estimateHeightForDate(wiz.net, field.text))
                        }
                    }
                }
                BdxTextField { id: kName; label: "Wallet name"; error: wiz.nameError(text) }
                RowLayout {
                    spacing: 12
                    BdxTextField { id: kPw; label: "New password"; echoMode: TextInput.Password; Layout.fillWidth: true }
                    BdxTextField { id: kPw2; label: "Repeat"; echoMode: TextInput.Password; Layout.fillWidth: true; error: text.length > 0 && text !== kPw.text ? "Passwords don't match" : "" }
                }
                RowLayout {
                    BdxButton { text: "Back"; onClicked: wiz.step = "home" }
                    Item { Layout.fillWidth: true }
                    BdxButton {
                        text: WalletManager.busy ? WalletManager.busyText : (kSpend.text.length ? "Restore wallet" : "Create view-only wallet")
                        variant: "primary"
                        enabled: WalletManager.addressValid(kAddr.text, wiz.net) && /^[0-9a-fA-F]{64}$/.test(kView.text)
                                 && (kSpend.text.length === 0 || /^[0-9a-fA-F]{64}$/.test(kSpend.text))
                                 && kName.text.length > 0 && wiz.nameError(kName.text) === "" && kPw.text === kPw2.text && !WalletManager.busy
                        onClicked: WalletManager.restoreFromKeys(App.walletDir, kName.text.trim(), kPw.text, wiz.net, Number(kHeight.text || 0),
                                                                 kAddr.text, kView.text, kSpend.text)
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------- open dialog
    BdxDialog {
        id: openDialog
        title: "Open wallet"
        dialogWidth: 480
        onOpened: { openPw.text = ""; openPw.field.forceActiveFocus() }
        Mono { text: WalletManager.prettyPath(wiz.openPath); color: Theme.green; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
        BdxTextField {
            id: openPw
            label: "Password"
            echoMode: TextInput.Password
            onAccepted: openBtn.clicked()
        }
        footer: [
            Item { Layout.fillWidth: true },
            BdxButton { text: "Cancel"; onClicked: openDialog.close() },
            BdxButton {
                id: openBtn
                text: WalletManager.busy ? "Opening…" : "Open"
                variant: "primary"
                enabled: !WalletManager.busy
                onClicked: { WalletManager.openWallet(wiz.openPath, openPw.text, wiz.net); openDialog.close() }
            }
        ]
    }

    FileDialog {
        id: fileDialog
        title: "Choose a Beldex wallet"
        nameFilters: ["Wallet keys (*.keys)", "All files (*)"]
        currentFolder: WalletManager.localFileToUrl(App.walletDir)
        onAccepted: { wiz.openPath = WalletManager.urlToLocalFile(selectedFile); openDialog.open() }
    }
    FolderDialog {
        id: folderDialog
        title: "Wallet folder"
        currentFolder: WalletManager.localFileToUrl(App.walletDir)
        onAccepted: { App.settings.walletDir = WalletManager.urlToLocalFile(selectedFolder); wiz.refreshWallets() }
    }

    Rectangle {
        anchors.fill: parent
        visible: WalletManager.busy
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea { anchors.fill: parent }
        Mono { anchors.centerIn: parent; text: WalletManager.busyText; color: Theme.green; font.pixelSize: 14 }
    }
}
