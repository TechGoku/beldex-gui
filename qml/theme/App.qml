pragma Singleton
import QtQuick
import QtCore
import BeldexGui

// App-wide state, persisted settings and UI service hooks (toasts, password prompt, navigation).
QtObject {
    id: app

    // --- navigation ----------------------------------------------------------------------------
    property string page: "overview"
    property var pageArgs: ({})
    function go(p, args) { pageArgs = args || ({}); page = p }

    // Wizard stays on screen after a create until the user has backed up the seed.
    property bool wizardHold: false

    // --- UI services (Main.qml connects these) -------------------------------------------------
    signal toastRequested(string text, string tone)
    signal passwordRequested(string message, var callback)
    function toast(text, tone) { toastRequested(text, tone || "info") }
    function askPassword(message, callback) { passwordRequested(message, callback) }
    signal txDetailsRequested(var tx)
    signal networkSwitchRequested()
    function switchNetwork() { networkSwitchRequested() }
    function showTx(tx) { txDetailsRequested(tx) }

    // --- settings --------------------------------------------------------------------------------
    property Settings settings: Settings {
        category: "beldex-gui"
        property string walletDir: ""
        property int netType: 0
        property string lastWalletPath: ""
        property string mainnetNode: "publicnode1.rpcnode.stream:29095"
        property string testnetNode: ""          // optional custom remote testnet node (Auto also tries the public one)
        property string devnetNode: ""
        // Per network: "auto" (public node if one answers, else a local beldexd), "remote" or "local".
        property string mainnetNodeMode: "auto"
        property string testnetNodeMode: "auto"
        property string devnetNodeMode: "auto"
        property string daemonPath: ""           // beldexd; empty = auto-detect
        property string daemonDataDir: ""        // empty = ~/.beldex (+ /testnet, /devnet)
        property string testnetPeers: ""         // extra peers for the local node, "host:port, host:port"
        property bool daemonPrune: false
        property string nodeUser: ""
        property string nodePassword: ""
        property bool nodeTrusted: false
        property bool hideBalance: false
    }

    readonly property string walletDir: settings.walletDir.length > 0 ? settings.walletDir : WalletManager.defaultWalletDir

    // Public remote nodes (from the Beldex Electron wallet). publicnode* verified live on 2026-11-24.
    readonly property var remoteNodes: [
        "publicnode1.rpcnode.stream:29095",
        "publicnode2.rpcnode.stream:29095",
        "publicnode3.rpcnode.stream:29095",
        "publicnode4.rpcnode.stream:29095",
        "publicnode5.rpcnode.stream:29095",
        "mainnet.beldex.io:29095"   // listed by the Electron wallet; not answering RPC on 2026-11-24
    ]
    // Public testnet node (testnet, hard fork v22, verified 2026-11-25). Without an answer, Auto runs a local node.
    readonly property var testnetRemoteNodes: [
        "209.126.86.93:29091"
    ]

    readonly property var netNames: ["MAINNET", "TESTNET", "DEVNET"]

    function nodeFor(net) {
        return net === 1 ? settings.testnetNode : net === 2 ? settings.devnetNode : settings.mainnetNode
    }
    function setNodeFor(net, address) {
        if (net === 1) settings.testnetNode = address
        else if (net === 2) settings.devnetNode = address
        else settings.mainnetNode = address
    }
    function publicNodesFor(net) { return net === 0 ? remoteNodes : net === 1 ? testnetRemoteNodes : [] }
    function nodeModeFor(net) {
        return net === 1 ? settings.testnetNodeMode : net === 2 ? settings.devnetNodeMode : settings.mainnetNodeMode
    }
    function setNodeModeFor(net, mode) {
        if (net === 1) settings.testnetNodeMode = mode
        else if (net === 2) settings.devnetNodeMode = mode
        else settings.mainnetNodeMode = mode
    }

    // --- connecting ------------------------------------------------------------------------------
    // What the wallet is (about to be) connected to, for the status strip.
    property string nodeState: ""            // "", "probing", "local-starting", "local", "remote"
    property bool usingLocalNode: nodeState === "local" || nodeState === "local-starting"
    // A local node that is still catching up can't give the wallet a true balance yet.
    readonly property bool nodeReady: !usingLocalNode || DaemonManager.synchronized
    property var _candidates: []
    property int _connectNet: 0

    function connectWallet() {
        const w = WalletManager.wallet
        if (!w) return
        _connectNet = w.netType
        const mode = nodeModeFor(w.netType)
        if (mode === "local") {
            startLocalNode(w.netType)
            return
        }
        // auto: the configured node, then the public list. remote: only the configured node
        // (the public list only if none is configured).
        const custom = nodeFor(w.netType)
        let list = custom.length ? [custom] : []
        if (mode === "auto" || list.length === 0)
            for (const n of publicNodesFor(w.netType))
                if (list.indexOf(n) < 0) list.push(n)
        if (list.length === 0) {
            if (mode === "auto") {
                toast(netNames[w.netType] + ": no public node available — starting a local node", "info")
                startLocalNode(w.netType)
            } else {
                toast("Set a node address in Settings › Node", "error")
            }
            return
        }
        _candidates = list
        nodeState = "probing"
        DaemonManager.probe(_candidates[0], w.netType)
    }

    function startLocalNode(net) {
        nodeState = "local-starting"
        DaemonManager.binaryPath = settings.daemonPath
        let extra = []
        if (net === 1)
            for (const p of settings.testnetPeers.split(/[\s,]+/).filter(x => x.length))
                extra.push("--add-peer", p)
        if (settings.daemonPrune)
            extra.push("--prune-blockchain")
        DaemonManager.start(net, settings.daemonDataDir, extra)
    }

    function _useRemote(address) {
        const w = WalletManager.wallet
        if (!w) return
        nodeState = "remote"
        w.connectToDaemon(address, settings.nodeUser, settings.nodePassword, settings.nodeTrusted)
    }

    property Connections _daemonEvents: Connections {
        target: DaemonManager
        function onProbed(address, ok, height, message) {
            if (app.nodeState !== "probing" || address !== app._candidates[0])
                return
            if (ok) {
                app._useRemote(address)
                return
            }
            console.info("node probe:", message)
            app._candidates = app._candidates.slice(1)
            if (app._candidates.length > 0) {
                DaemonManager.probe(app._candidates[0], app._connectNet)
            } else if (app.nodeModeFor(app._connectNet) === "auto") {
                app.toast("No public node answered — starting a local node", "info")
                app.startLocalNode(app._connectNet)
            } else {
                app.nodeState = ""
                app.toast(message, "error")
                // still hand the address to the wallet so it keeps retrying
                if (WalletManager.wallet) WalletManager.wallet.connectToDaemon(app.nodeFor(app._connectNet), app.settings.nodeUser, app.settings.nodePassword, app.settings.nodeTrusted)
            }
        }
        function onReadyForWallet(rpcAddress) {
            const w = WalletManager.wallet
            if (!w || app.nodeState !== "local-starting" || w.netType !== DaemonManager.netType)
                return
            app.nodeState = "local"
            w.connectToDaemon(rpcAddress, "", "", true)   // our own node: trusted
        }
        function onError(message) {
            if (app.usingLocalNode) {
                app.toast(message, "error")
                app.nodeState = ""
            }
        }
    }

    // --- links -----------------------------------------------------------------------------------
    function explorerBase(net) { return net === 0 ? "https://explorer.beldex.io" : "https://testnet.beldex.dev" }
    function openTx(hash) { Qt.openUrlExternally(explorerBase(WalletManager.wallet ? WalletManager.wallet.netType : 0) + "/tx/" + hash) }
    function openMasterNode(key) { Qt.openUrlExternally(explorerBase(WalletManager.wallet ? WalletManager.wallet.netType : 0) + "/mn/" + key) }

    // --- formatting ------------------------------------------------------------------------------
    function money(v) { return settings.hideBalance ? "••••••" : v }
    function fmtHeight(h) { return Number(h).toLocaleString(Qt.locale("en_US"), "f", 0) }
    // Block height -> approx. date, using Beldex's 30 s block time.
    function heightToDate(h) {
        const w = WalletManager.wallet
        if (!w || w.daemonHeight <= 0) return ""
        const d = new Date(Date.now() + (h - w.daemonHeight) * 30 * 1000)
        return d.toISOString().slice(0, 10)
    }
}
