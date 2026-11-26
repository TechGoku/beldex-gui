import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import BeldexGui

BdxPage {
    id: page
    readonly property var w: WalletManager.wallet
    readonly property var h: w ? w.history : null

    title: "Transaction history"
    subtitle: h ? h.count + " of " + h.totalCount + " transactions" : ""

    headerRight: [
        BdxButton { text: "Export CSV"; onClicked: csvDialog.open() }
    ]

    RowLayout {
        Layout.fillWidth: true
        spacing: 16
        ChipGroup {
            current: page.h ? page.h.filter : "all"
            model: [
                { value: "all", label: "All" }, { value: "in", label: "In" }, { value: "out", label: "Out" },
                { value: "reward", label: "Rewards" }, { value: "stake", label: "Stakes" },
                { value: "bns", label: "BNS" }
            ].concat(page.w && page.w.tokensEnabled ? [{ value: "token", label: "Tokens" }] : [])
             .concat([{ value: "pending", label: "Pending" }])
            onPicked: v => page.h.filter = v
        }
        Item { Layout.fillWidth: true }
        BdxTextField {
            placeholderText: page.w && page.w.tokensEnabled ? "Search tx id, note, address, height or ticker" : "Search tx id, note, address or height"
            Layout.preferredWidth: 380
            Layout.fillWidth: false
            field.onTextChanged: page.h.search = field.text
        }
    }

    Panel {
        Layout.fillWidth: true
        contentPadding: 0
        spacing: 0
        TxTable {
            Layout.fillWidth: true
            listHeight: Math.max(440, page.height - 340)
            model: page.h
            showUnits: page.w && page.w.tokensEnabled
            emptyText: page.h && page.h.totalCount > 0 ? "Nothing matches this filter" : "No transactions yet"
            onActivated: tx => App.showTx(tx)
        }
    }

    FileDialog {
        id: csvDialog
        title: "Export transaction history"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: ["CSV files (*.csv)"]
        currentFile: "file:///" + (page.w ? page.w.name : "wallet") + "-history.csv"
        onAccepted: {
            const ok = WalletManager.saveTextFile(selectedFile, page.h.exportCsv())
            App.toast(ok ? "History exported" : "Could not write file", ok ? "ok" : "error")
        }
    }
}
