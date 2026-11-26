import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BeldexGui

BdxPage {
    id: page
    readonly property var w: WalletManager.wallet
    property string feeEstimate: "—"
    // Testnet: "" sends BDX, a token id sends that privacy token (the fee is still BDX).
    property string asset: ""
    readonly property var token: {
        if (!w || asset === "") return null
        for (const t of w.tokens) if (t.tokenId === asset) return t
        return null
    }
    readonly property string unit: token ? token.ticker : "BDX"
    readonly property int decimals: token ? token.decimals : 9
    onTokenChanged: if (asset !== "" && token === null) asset = ""   // token spent or account switched

    title: "Send " + unit
    subtitle: !w ? "" : "From account #" + w.accountIndex + " · unlocked "
              + (token ? App.money(token.unlocked) + " " + token.ticker : App.money(w.unlockedBalance) + " BDX")

    ListModel { id: recipients; ListElement { address: ""; amount: "" } }

    function destinations() {
        const out = []
        for (let i = 0; i < recipients.count; ++i)
            out.push({ address: recipients.get(i).address, amount: recipients.get(i).amount })
        return out
    }
    function reset() {
        recipients.clear()
        recipients.append({ address: "", amount: "" })
    }
    // The fee needs the node (fork rules + base fee), so only ask once connected.
    function refreshFee() { if (w && w.connected && w.synchronized) w.estimateFee(recipients.count, flash.checked) }

    Connections {
        target: page.w
        function onFeeEstimated(fee) { page.feeEstimate = fee }
        function onSynchronizedChanged() { page.refreshFee() }
        function onTransactionCommitted(success) { if (success) page.reset() }
    }
    Component.onCompleted: refreshFee()

    // Prefill from Contacts / URI / Tokens. App.go() sets pageArgs before page, so apply on either change.
    function applyArgs() {
        if (App.page !== "send") return
        if (App.pageArgs.token !== undefined)
            page.asset = App.pageArgs.token
        if (App.pageArgs.address) {
            recipients.setProperty(0, "address", App.pageArgs.address)
            if (App.pageArgs.amount) recipients.setProperty(0, "amount", App.pageArgs.amount)
        }
    }
    Connections {
        target: App
        function onPageArgsChanged() { page.applyArgs() }
        function onPageChanged() { page.applyArgs() }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 22

        Panel {
            title: "Recipients"
            note: recipients.count + (recipients.count === 1 ? " destination" : " destinations")
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop

            RowLayout {
                visible: page.w.tokensEnabled
                spacing: 12
                Caption { text: "Asset" }
                BdxButton {
                    text: (page.token ? page.token.ticker + "  ·  " + App.money(page.token.unlocked) + " unlocked" : "BDX") + "  ▾"
                    onClicked: assetMenu.popup()
                }
                Mono {
                    text: page.w.tokens.length === 0 ? "No privacy tokens in this account yet" : "Token transfers pay their fee in BDX"
                    color: Theme.textFaint
                    font.pixelSize: 11
                }
            }

            Repeater {
                model: recipients
                delegate: RowLayout {
                    id: rcpt
                    required property int index
                    required property string address
                    required property string amount
                    readonly property bool addrOk: address.length === 0 || WalletManager.addressValid(address, page.w.netType)
                    readonly property bool amtOk: amount.length === 0
                        || (page.asset === "" ? WalletManager.amountValid(amount) : page.w.tokenAmountValid(amount, page.asset))
                    Layout.fillWidth: true
                    spacing: 12

                    BdxTextField {
                        label: recipients.count > 1 ? "Address #" + (rcpt.index + 1) : "Recipient address"
                        placeholderText: "bx… or beldex: payment link"
                        text: rcpt.address
                        monoSmall: true
                        error: rcpt.addrOk ? "" : "Not a valid " + page.w.netName.toLowerCase() + " address"
                        Layout.fillWidth: true
                        field.onTextEdited: {
                            let v = field.text.trim()
                            if (v.toLowerCase().startsWith("beldex:")) {
                                const p = WalletManager.parseUri(v)
                                v = p.address
                                if (p.amount) recipients.setProperty(rcpt.index, "amount", p.amount)
                            }
                            recipients.setProperty(rcpt.index, "address", v)
                        }
                        trailing: BdxButton {
                            compact: true
                            text: "Book"
                            visible: page.w.addressBook.length > 0
                            onClicked: bookMenu.openFor(rcpt.index)
                        }
                    }
                    BdxTextField {
                        label: "Amount"
                        placeholderText: "0.00"
                        suffix: page.unit
                        text: rcpt.amount
                        Layout.preferredWidth: 220
                        error: rcpt.amtOk ? "" : "Invalid amount"
                        validator: RegularExpressionValidator {
                            regularExpression: page.asset === "" ? /^\d{0,10}(\.\d{0,9})?$/
                                : page.decimals > 0 ? new RegExp("^\\d{0,20}(\\.\\d{0," + page.decimals + "})?$") : /^\d{0,20}$/
                        }
                        field.onTextEdited: recipients.setProperty(rcpt.index, "amount", field.text)
                    }
                    BdxButton {
                        text: "×"
                        compact: true
                        visible: recipients.count > 1
                        Layout.topMargin: 18
                        onClicked: { recipients.remove(rcpt.index); page.refreshFee() }
                    }
                }
            }

            RowLayout {
                spacing: 10
                BdxButton {
                    text: "+ Add recipient"
                    compact: true
                    enabled: recipients.count < 15
                    onClicked: { recipients.append({ address: "", amount: "" }); page.refreshFee() }
                }
                Item { Layout.fillWidth: true }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

            BdxSwitch {
                id: flash
                text: "Flash transaction"
                description: "Confirmed by master node quorums in seconds instead of waiting for a block. Costs about 3× the normal fee."
                Layout.fillWidth: true
                onToggled: page.refreshFee()
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 12
                ColumnLayout {
                    spacing: 6
                    Caption { text: "Estimated fee" }
                    Mono { text: page.feeEstimate + " BDX"; color: flash.checked ? Theme.amber : Theme.text; font.bold: true }
                }
                Item { Layout.fillWidth: true }
                BdxButton {
                    text: page.asset === "" ? "Send all" : "Send all " + page.unit
                    enabled: page.w.synchronized && !page.w.busy
                    // BDX: the whole balance to this account (as before). A token: all of it to the
                    // recipient above, or to this account when no single address is filled in.
                    onClicked: page.asset === "" ? page.w.createSweepAll(flash.checked)
                        : page.w.sweep(page.asset, recipients.count === 1 ? recipients.get(0).address.trim() : "", flash.checked)
                }
                BdxButton {
                    text: page.w.busy ? page.w.busyText : "Review & send"
                    variant: "primary"
                    enabled: page.w.connected && page.w.synchronized && App.nodeReady && !page.w.busy
                    onClicked: page.asset === "" ? page.w.createTransaction(page.destinations(), flash.checked)
                        : page.w.createTransaction(page.destinations(), flash.checked, page.asset)
                }
            }
            Mono {
                visible: !page.w.synchronized || !App.nodeReady
                text: !App.nodeReady ? "Sending unlocks once the local node has caught up with the network."
                    : page.w.connected ? "Sending unlocks once the wallet has finished syncing." : "Connect to a node first (Settings › Node)."
                color: Theme.amber
                font.pixelSize: 11
            }
        }

        Panel {
            title: "Network parameters"
            Layout.preferredWidth: 360
            Layout.alignment: Qt.AlignTop
            KeyValue { key: "Base fee"; keyWidth: 100; value: "0.0001 BDX / output\n+ 0.006666 BDX / kB" }
            KeyValue { key: "Flash fee"; keyWidth: 100; value: "0.0003 BDX / output\n+ 0.019998 BDX / kB" }
            KeyValue { key: "Unlock"; keyWidth: 100; value: "10 blocks (~5 min)" }
            KeyValue { key: "Block time"; keyWidth: 100; value: "30 seconds" }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
            Mono {
                text: "Amounts and recipients are encrypted on-chain with ring signatures, stealth addresses and RingCT."
                color: Theme.textFaint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    Menu {
        id: assetMenu
        MenuItem { text: "BDX"; font.family: Theme.mono; onTriggered: page.asset = "" }
        Instantiator {
            model: page.w && page.w.tokensEnabled ? page.w.tokens : []
            delegate: MenuItem {
                required property var modelData
                text: modelData.ticker + "  ·  " + modelData.unlocked + " unlocked" + (modelData.fullName ? "  ·  " + modelData.fullName : "")
                font.family: Theme.mono
                onTriggered: page.asset = modelData.tokenId
            }
            onObjectAdded: (index, object) => assetMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => assetMenu.removeItem(object)
        }
    }

    Menu {
        id: bookMenu
        property int target: 0
        function openFor(i) { target = i; popup() }
        Instantiator {
            model: page.w ? page.w.addressBook : []
            delegate: MenuItem {
                required property var modelData
                text: modelData.description + "  ·  " + WalletManager.shortAddress(modelData.address, 6)
                font.family: Theme.mono
                onTriggered: recipients.setProperty(bookMenu.target, "address", modelData.address)
            }
            onObjectAdded: (index, object) => bookMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => bookMenu.removeItem(object)
        }
    }
}
