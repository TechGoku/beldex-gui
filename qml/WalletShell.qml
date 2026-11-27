import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BeldexGui

// Open-wallet layout: status strip, header with logo + nav, sync bar, page stack.
ColumnLayout {
    id: shell
    spacing: 0
    readonly property var wallet: WalletManager.wallet

    // Same order as the StackLayout below. Tokens exist on testnet only, so mainnet never shows that tab.
    readonly property var allPages: [
        { id: "overview", title: "Overview" },
        { id: "send", title: "Send" },
        { id: "receive", title: "Receive" },
        { id: "history", title: "History" },
        { id: "staking", title: "Master Nodes" },
        { id: "bns", title: "BNS" },
        { id: "tokens", title: "Tokens" },
        { id: "contacts", title: "Contacts" },
        { id: "settings", title: "Settings" }
    ]
    readonly property var pages: allPages.filter(p => p.id !== "tokens" || (wallet && wallet.tokensEnabled))

    // --- status strip (explorer: "BDX Price … Height … ◈ MAINNET") ----------------------------
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 34
        color: Theme.header
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.gutter
            anchors.rightMargin: Theme.gutter
            spacing: 22
            Row {
                spacing: 8
                Mono { text: "Balance:"; color: Theme.textDim; font.pixelSize: 11 }
                Mono { text: App.money(shell.wallet.balance) + " BDX"; color: Theme.green; font.bold: true; font.pixelSize: 11 }
            }
            Row {
                spacing: 8
                Mono { text: "Height:"; color: Theme.textDim; font.pixelSize: 11 }
                Mono { text: shell.wallet.daemonHeight > 0 ? App.fmtHeight(shell.wallet.daemonHeight) : "—"; color: Theme.green; font.bold: true; font.pixelSize: 11 }
            }
            Row {
                spacing: 8
                Rectangle {
                    width: 7; height: 7; radius: 3.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: App.usingLocalNode && !DaemonManager.synchronized ? Theme.amber
                         : !shell.wallet.connected ? Theme.red : shell.wallet.synchronized ? Theme.green : Theme.amber
                }
                Mono {
                    color: Theme.textDim
                    font.pixelSize: 11
                    text: App.nodeState === "probing" ? "Finding a node…"
                        : App.usingLocalNode && !DaemonManager.synchronized ? DaemonManager.statusText
                        : shell.wallet.connectionStatus === 2 ? "Node version mismatch"
                        : !shell.wallet.connected && App.nodeModeFor(shell.wallet.netType) !== "remote" && DaemonManager.lastError.length > 0
                          ? "Local node: " + DaemonManager.lastError
                        : !shell.wallet.connected ? (shell.wallet.busy ? shell.wallet.busyText
                                                   : "Disconnected" + (App.nodeFor(shell.wallet.netType).length ? " · " + App.nodeFor(shell.wallet.netType) : ""))
                        : shell.wallet.synchronized ? "Synced · " + (App.usingLocalNode ? "local node" : shell.wallet.daemonAddress)
                        : "Syncing " + (shell.wallet.syncProgress * 100).toFixed(1) + "% · " + App.fmtHeight(shell.wallet.blockHeight) + " / " + App.fmtHeight(shell.wallet.daemonHeight)
                          + (shell.wallet.syncSecondsLeft >= 0 ? " · " + App.fmtTimeLeft(shell.wallet.syncSecondsLeft) : "")
                }
            }
            Item { Layout.fillWidth: true }
            // Network badge; click to switch this wallet to another network.
            Rectangle {
                implicitHeight: 24
                implicitWidth: netText.implicitWidth + 22
                color: netMouse.containsMouse ? "#121212" : "transparent"
                border.color: shell.wallet.netType === 0 ? Theme.green : Theme.amber
                MouseArea {
                    id: netMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: App.switchNetwork()
                }
                Text {
                    id: netText
                    anchors.centerIn: parent
                    text: "◈ " + shell.wallet.netName + "  ▾"
                    color: shell.wallet.netType === 0 ? Theme.green : Theme.amber
                    font.family: Theme.display
                    font.pixelSize: 9
                    font.letterSpacing: 1.8
                    renderType: Text.NativeRendering
                }
            }
        }
    }

    // --- header: logo + nav -------------------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 68
        color: Qt.rgba(0.02, 0.02, 0.02, 0.92)
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.gutter
            anchors.rightMargin: Theme.gutter
            spacing: 12

            Image {
                source: "qrc:/qt/qml/BeldexGui/images/logo-wordmark.png"
                sourceSize.height: 26
                fillMode: Image.PreserveAspectFit
                Layout.preferredHeight: 26
                smooth: true
                mipmap: true
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: App.go("overview") }
            }
            Mono {
                text: shell.wallet.name
                color: Theme.textFaint
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 160
                visible: shell.width > 1450
            }
            Item { Layout.fillWidth: true }

            Repeater {
                model: shell.pages
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool active: App.page === modelData.id
                    implicitHeight: 34
                    implicitWidth: navText.implicitWidth + (shell.width > 1400 ? 28 : 16)
                    color: active ? Theme.greenDim : navMouse.containsMouse ? "#101010" : "transparent"
                    border.width: active ? 1 : 0
                    border.color: Theme.borderStrong
                    Text {
                        id: navText
                        anchors.centerIn: parent
                        text: modelData.title
                        color: parent.active ? Theme.greenNav : navMouse.containsMouse ? Theme.text : Theme.textDim
                        font.family: Theme.display
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: shell.width > 1400 ? 1.9 : 1.4
                        font.capitalization: Font.AllUppercase
                        renderType: Text.NativeRendering
                    }
                    MouseArea {
                        id: navMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: App.go(modelData.id)
                    }
                }
            }
        }
    }

    // --- sync progress ------------------------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 2
        color: "transparent"
        visible: shell.wallet.connected && !shell.wallet.synchronized
        Rectangle {
            height: parent.height
            width: parent.width * shell.wallet.syncProgress
            color: Theme.green
            Behavior on width { NumberAnimation { duration: 400 } }
        }
    }

    // --- pages --------------------------------------------------------------------------------
    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: Math.max(0, shell.pages.findIndex(p => p.id === App.page) < 0 ? 0
                                  : shell.allPages.findIndex(p => p.id === App.page))

        OverviewPage {}
        SendPage {}
        ReceivePage {}
        HistoryPage {}
        StakingPage {}
        BnsPage {}
        Loader { active: shell.wallet && shell.wallet.tokensEnabled; sourceComponent: Component { TokensPage {} } }
        ContactsPage {}
        SettingsPage {}
    }
}
