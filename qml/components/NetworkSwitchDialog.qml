import QtQuick
import QtQuick.Layouts
import BeldexGui

// Moves the open wallet to another network. A wallet file belongs to one network, so the same seed is kept as
// a separate wallet per network, next to the original (WalletManager.networkSibling); switching saves and
// closes this one and opens (or first creates) that one.
BdxDialog {
    id: root
    title: "Switch network"
    dialogWidth: 560

    property int target: -1
    property var sibling: ({})
    property string probeAddress: ""
    property double nodeHeight: 0
    readonly property var w: WalletManager.wallet
    readonly property int current: w ? w.netType : -1
    readonly property string currentName: current >= 0 ? App.netNames[current] : ""
    readonly property string targetName: target >= 0 ? App.netNames[target] : ""
    readonly property bool isNew: target !== current && sibling.exists === false

    function show() {
        if (!w)
            return
        pw.text = ""
        pick(current === 0 ? 1 : 0)
        open()
        pw.field.forceActiveFocus()
    }

    function pick(net) {
        target = net
        netChips.current = net   // the chips' own click handler replaces any binding, so set it here
        sibling = WalletManager.networkSibling(net)
        pw.error = ""
        heightField.error = ""
        heightField.text = ""
        nodeHeight = 0
        probeAddress = ""
        scan.current = "new"
        if (!isNew)
            return
        // "New on this network" starts at the current block: ask that network's node how far it is.
        const custom = App.nodeFor(net)
        const list = custom.length ? [custom] : App.publicNodesFor(net)
        if (list.length > 0 && App.nodeModeFor(net) !== "local") {
            probeAddress = list[0]
            DaemonManager.probe(probeAddress, net)
        } else {
            scan.current = "full"
        }
    }

    function submit() {
        if (!w || target === current || WalletManager.busy)
            return
        let height = 0
        if (isNew) {
            if (scan.current === "new") {
                if (nodeHeight <= 0) {
                    pw.error = "Still asking the " + targetName.toLowerCase() + " node for its height. Try again, or pick another start."
                    return
                }
                height = Math.max(0, nodeHeight - 20)   // a little margin for blocks mined meanwhile
            } else if (scan.current === "block") {
                height = parseInt(heightField.text, 10)
                if (isNaN(height) || height < 0) {
                    heightField.error = "Enter a block number"
                    return
                }
            }
        }
        const err = WalletManager.switchNetwork(target, pw.text, height)
        if (err.length > 0) {
            pw.error = err
            return
        }
        pw.text = ""
        close()
    }

    Connections {
        target: DaemonManager
        function onProbed(address, ok, height, message) {
            if (!root.opened || address !== root.probeAddress)
                return
            root.probeAddress = ""
            if (ok && height > 0) {
                root.nodeHeight = height
            } else if (scan.current === "new") {
                scan.current = "full"
            }
        }
    }

    ChipGroup {
        id: netChips
        model: WalletManager.showTestNetworks
               ? [{ value: 0, label: "Mainnet" }, { value: 1, label: "Testnet" }, { value: 2, label: "Devnet" }]
               : [{ value: 0, label: "Mainnet" }]
        onPicked: v => root.pick(v)
    }

    Mono {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim
        lineHeight: 1.3
        text: root.target === root.current
              ? "This wallet is open on " + root.currentName + "."
              : root.isNew
                ? "Creates a " + root.targetName + " copy of this wallet from its seed (the same keys, with "
                  + root.targetName + " addresses), saved next to it as “" + root.sibling.name + "”. Your "
                  + root.currentName + " wallet isn't changed: switch back here any time."
                  + (root.target !== 0 ? " The first sync downloads the " + root.targetName.toLowerCase()
                     + " block list, which takes several minutes; later switches open quickly." : "")
                : "Opens “" + root.sibling.name + "”, this wallet's " + root.targetName + " copy. Your "
                  + root.currentName + " wallet is saved and closed first."
    }

    ColumnLayout {
        visible: root.isNew
        spacing: 8
        Layout.fillWidth: true
        Caption { text: "Scan " + root.targetName.toLowerCase() + " history from" }
        ChipGroup {
            id: scan
            model: [{ value: "new", label: "Now (new account)" }, { value: "full", label: "Full history" }, { value: "block", label: "Block…" }]
            current: "new"
        }
        Mono {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: 11
            text: scan.current === "new"
                  ? (root.nodeHeight > 0 ? "Starts at block " + App.fmtHeight(root.nodeHeight) + ". Pick Full history if this seed was already used on " + root.targetName.toLowerCase() + "."
                     : root.probeAddress.length > 0 ? "Asking " + root.probeAddress + " for the current block…"
                     : "No " + root.targetName.toLowerCase() + " node answered; pick Full history or a block.")
                  : scan.current === "full" ? "Scans every block from the start. Slower, but finds any earlier " + root.targetName.toLowerCase() + " transactions."
                  : "Scans from the block you enter."
        }
        BdxTextField {
            id: heightField
            visible: scan.current === "block"
            label: "Block height"
            placeholderText: "0"
            inputMethodHints: Qt.ImhDigitsOnly
            onAccepted: root.submit()
        }
    }

    BdxTextField {
        id: pw
        visible: root.target !== root.current
        label: root.isNew || root.sibling.exists !== true ? "Wallet password" : "Password of " + root.sibling.name
        help: root.isNew ? "The copy gets the same password." : ""
        echoMode: TextInput.Password
        onAccepted: root.submit()
    }

    footer: [
        Item { Layout.fillWidth: true },
        BdxButton { text: "Cancel"; onClicked: root.close() },
        BdxButton {
            text: WalletManager.busy ? WalletManager.busyText : root.isNew ? "Create & switch" : "Switch"
            variant: "primary"
            enabled: root.target !== root.current && !WalletManager.busy
            onClicked: root.submit()
        }
    ]
}
