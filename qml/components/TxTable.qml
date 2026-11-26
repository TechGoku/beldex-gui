import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BeldexGui

// Explorer-style transaction table. `model` is TransactionHistoryModel or a JS array of maps.
ColumnLayout {
    id: root
    property var model
    property bool scrollable: true
    property string emptyText: "No transactions yet"
    property int listHeight: 300
    property bool showUnits: false   // testnet: rows may be privacy tokens, so every amount names its unit
    signal activated(var tx)
    spacing: 0

    readonly property var cols: [
        { key: "date", title: "Date", w: 150 },
        { key: "type", title: "Type", w: 120 },
        { key: "hash", title: "Transaction hash", w: -1 },
        { key: "amount", title: root.showUnits ? "Amount" : "Amount (BDX)", w: root.showUnits ? 230 : 170 },
        { key: "conf", title: "Height", w: 110 }
    ]

    // header row (#242424 like the explorer)
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 38
        color: Theme.row
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.pad
            anchors.rightMargin: Theme.pad
            spacing: 12
            Repeater {
                model: root.cols
                Mono {
                    required property var modelData
                    text: modelData.title
                    color: Theme.text
                    Layout.preferredWidth: modelData.w > 0 ? modelData.w : -1
                    Layout.fillWidth: modelData.w < 0
                    horizontalAlignment: modelData.key === "amount" ? Text.AlignRight : Text.AlignLeft
                }
            }
        }
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: root.scrollable
        implicitHeight: root.scrollable ? root.listHeight : contentHeight
        interactive: root.scrollable
        clip: true
        model: root.model
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: root.scrollable ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

        delegate: Rectangle {
            id: rowItem
            // Works for both a QAbstractItemModel (roles) and a JS array (modelData).
            // JS/QVariantList models expose rows as modelData; TransactionHistoryModel exposes roles.
            readonly property bool fromList: model.hash === undefined
            readonly property var tx: fromList ? modelData : model
            width: ListView.view.width
            height: 44
            color: hover.containsMouse ? Theme.rowHover : "transparent"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.pad
                anchors.rightMargin: Theme.pad
                spacing: 12
                Mono { text: rowItem.tx.dateText; color: Theme.textDim; Layout.preferredWidth: 150 }
                Item {
                    Layout.preferredWidth: 120
                    implicitHeight: 20
                    KindBadge { kind: rowItem.tx.kind; pending: rowItem.tx.pending; anchors.verticalCenter: parent.verticalCenter }
                }
                Mono {
                    text: rowItem.tx.hash
                    color: Theme.green
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Mono {
                    readonly property bool zero: rowItem.tx.amount === "0.00" || rowItem.tx.amount === "0"
                    readonly property string unitText: root.showUnits && rowItem.tx.unit ? " " + rowItem.tx.unit : ""
                    readonly property string sign: rowItem.tx.kind === "token-register" || rowItem.tx.kind === "token-mint" ? "+"
                                                 : rowItem.tx.incoming ? "+" : "−"
                    text: zero ? "—" : sign + rowItem.tx.amount + unitText
                    elide: Text.ElideLeft
                    color: zero ? Theme.textFaint : rowItem.tx.incoming ? Theme.green : Theme.text
                    font.bold: true
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: root.showUnits ? 230 : 170
                }
                Mono {
                    text: rowItem.tx.pending ? "mempool" : Number(rowItem.tx.height).toFixed(0)
                    color: rowItem.tx.pending ? Theme.amber : Theme.textDim
                    Layout.preferredWidth: 110
                }
            }
            MouseArea {
                id: hover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.activated(rowItem.fromList ? rowItem.tx : root.model.get(index))
            }
        }

        Mono {
            anchors.centerIn: parent
            visible: root.scrollable && list.count === 0
            text: root.emptyText
            color: Theme.textFaint
            font.italic: true
        }
    }
    // Non-scrolling tables collapse to zero rows, so show the empty message below the header instead.
    Item {
        visible: !root.scrollable && list.count === 0
        implicitHeight: 64
        Layout.fillWidth: true
        Mono { anchors.centerIn: parent; text: root.emptyText; color: Theme.textFaint; font.italic: true }
    }
}
