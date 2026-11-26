import QtQuick
import QtQuick.Layouts
import BeldexGui

// Privacy tokens (Beldex hard fork 22, testnet only): everything the CLI's token commands do —
// balance, transfer and sweep a token, register_privacy_token, mint_token, burn_token, update_token,
// tokens_by_owner — plus a searchable copy of the chain's token registry.
BdxPage {
    id: page
    readonly property var w: WalletManager.wallet
    readonly property var reg: w ? w.tokenRegistry : null

    property string search: ""
    property string ownerFilter: ""
    readonly property string ownerKey: ownerFilter.trim().length > 0 && w ? w.ownerKeyOf(ownerFilter) : ""
    readonly property bool ownerBad: ownerFilter.trim().length > 0 && ownerKey.length === 0
    property int regRev: 0
    readonly property var results: {
        regRev
        if (!reg || ownerBad) return []
        return reg.search(search, ownerKey, 100)
    }
    property string sweepWhat: "bdx"

    title: "Privacy tokens"
    subtitle: "Testnet · create, hold and send privacy tokens. Amounts are private like BDX; network fees are paid in BDX."

    headerRight: [
        BdxButton {
            text: page.reg && page.reg.loading ? "Reading registry " + page.reg.loaded + " / " + page.reg.total : "Refresh tokens"
            enabled: page.reg && !page.reg.loading && page.w.connected
            onClicked: page.w.refreshTokens()
        }
    ]

    Connections {
        target: page.reg
        function onChanged() { page.regRev++ }
    }
    Connections {
        target: page.w
        function onTransactionCommitted(success) {
            if (!success) return
            if (actionDialog.opened) actionDialog.close()
            createForm.clear()
        }
    }

    function shortKey(k) { return k.length > 20 ? k.substring(0, 8) + "…" + k.substring(k.length - 8) : k }
    function isMine(owner) { return page.w && owner.toLowerCase() === page.w.spendPublicKey }

    StatGrid {
        Layout.fillWidth: true
        columns: 4
        StatTile { label: "Tokens held"; value: String(page.w.tokens.length); sub: "account #" + page.w.accountIndex }
        StatTile { label: "Created by you"; value: String(page.w.ownedTokens.length); sub: "you can mint and update these" }
        StatTile {
            label: "Registered on testnet"
            value: page.reg && page.reg.count > 0 ? String(page.reg.count) : "—"
            sub: page.reg && page.reg.loading ? "reading " + page.reg.loaded + " / " + page.reg.total
                 : page.reg && page.reg.error.length > 0 ? page.reg.error
                 : page.reg && page.reg.updated.length > 0 ? "as of " + page.reg.updated : "not read yet"
        }
        StatTile { label: "BDX for fees"; value: App.money(page.w.unlockedBalance); unit: "BDX"; sub: "unlocked" }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 22

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 22

            // --- balances --------------------------------------------------------------------
            Panel {
                title: "Balances"
                note: "account #" + page.w.accountIndex
                contentPadding: 0
                spacing: 0
                Layout.fillWidth: true

                TableHeader { cols: [["Token", -1], ["Balance", 170], ["Unlocked", 170], ["", 150]] }
                Repeater {
                    model: page.w.tokens
                    delegate: TableRow {
                        id: bal
                        required property var modelData
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Mono { text: bal.modelData.ticker; color: Theme.green; font.bold: true }
                            Mono { text: bal.modelData.fullName; color: Theme.textDim; elide: Text.ElideRight; Layout.fillWidth: true }
                            Mono { text: "yours"; color: Theme.blue; font.pixelSize: 10; visible: bal.modelData.mine }
                        }
                        Mono { text: App.money(bal.modelData.balance); horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 170; font.bold: true }
                        Mono {
                            text: App.money(bal.modelData.unlocked)
                            color: bal.modelData.locked ? Theme.amber : Theme.text
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 170
                        }
                        RowLayout {
                            Layout.preferredWidth: 150
                            Layout.fillWidth: false   // nested layouts fill by default, which shifts the columns
                            spacing: 6
                            Item { Layout.fillWidth: true }
                            BdxButton { compact: true; text: "Send"; variant: "accent"; onClicked: App.go("send", { token: bal.modelData.tokenId }) }
                            BdxButton { compact: true; text: "Burn"; onClicked: actionDialog.show("burn", bal.modelData.tokenId) }
                        }
                    }
                }
                Mono {
                    visible: page.w.tokens.length === 0
                    text: page.w.synchronized ? "No tokens in this account. Tokens sent to your address show up here."
                                              : "Token balances appear once the wallet has synced."
                    color: Theme.textFaint
                    font.italic: true
                    Layout.margins: Theme.pad
                }
            }

            // --- tokens this wallet owns ------------------------------------------------------
            Panel {
                title: "Your tokens"
                note: page.reg && page.reg.count === 0 && !page.reg.loading ? "registry not read yet" : "registered by this wallet"
                contentPadding: 0
                spacing: 0
                Layout.fillWidth: true

                Repeater {
                    model: page.w.ownedTokens
                    delegate: Rectangle {
                        id: own
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: ownCol.implicitHeight + 28
                        color: "transparent"
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                        ColumnLayout {
                            id: ownCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: Theme.pad
                            anchors.rightMargin: Theme.pad
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                Mono { text: own.modelData.ticker; color: Theme.green; font.pixelSize: 16; font.bold: true }
                                Mono { text: own.modelData.fullName; color: Theme.textDim; elide: Text.ElideRight; Layout.fillWidth: true }
                                BdxButton { compact: true; text: "Mint"; variant: "accent"; onClicked: actionDialog.show("mint", own.modelData.tokenId) }
                                BdxButton { compact: true; text: "Update"; onClicked: actionDialog.show("update", own.modelData.tokenId) }
                                BdxButton { compact: true; text: "Burn"; onClicked: actionDialog.show("burn", own.modelData.tokenId) }
                            }
                            KeyValue { key: "Supply"; keyWidth: 110; value: own.modelData.supply + " of " + own.modelData.maxSupply + " " + own.modelData.ticker }
                            KeyValue { key: "You hold"; keyWidth: 110; value: own.modelData.held + " " + own.modelData.ticker }
                            KeyValue { key: "Decimals"; keyWidth: 110; value: String(own.modelData.decimals) }
                            KeyValue { key: "Metadata"; keyWidth: 110; value: own.modelData.metaInfo; visible: own.modelData.metaInfo.length > 0 }
                            CopyField { label: "Token ID"; value: own.modelData.tokenId }
                        }
                    }
                }
                Mono {
                    visible: page.w.ownedTokens.length === 0
                    text: page.reg && page.reg.loading ? "Reading the token registry…"
                        : "You haven't registered a token. Create one on the right."
                    color: Theme.textFaint
                    font.italic: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.margins: Theme.pad
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 440
            Layout.maximumWidth: 440
            Layout.alignment: Qt.AlignTop
            spacing: 22

            // --- register_privacy_token -------------------------------------------------------
            Panel {
                id: createForm
                title: "Create a token"
                Layout.fillWidth: true
                function clear() { for (const f of [ticker, fullName, maxSupply, supply, meta]) f.text = ""; decimals.text = "8" }

                BdxTextField {
                    id: ticker
                    label: "Ticker"
                    placeholderText: "e.g. BLINK"
                    help: "1–14 letters or digits"
                    validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9]{0,14}$/ }
                }
                BdxTextField {
                    id: fullName
                    label: "Name"
                    placeholderText: "e.g. Blink Rewards"
                    validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9 _.\-]{0,64}$/ }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    BdxTextField {
                        id: decimals
                        label: "Decimals"
                        text: "8"
                        Layout.preferredWidth: 110
                        Layout.fillWidth: false
                        validator: IntValidator { bottom: 0; top: 18 }
                    }
                    BdxTextField {
                        id: maxSupply
                        label: "Maximum supply"
                        placeholderText: "1000000"
                        suffix: ticker.text
                        Layout.fillWidth: true
                        validator: RegularExpressionValidator { regularExpression: /^\d{0,20}(\.\d{0,18})?$/ }
                    }
                }
                BdxTextField {
                    id: supply
                    label: "Initial supply (sent to you)"
                    placeholderText: "0 — mint later"
                    suffix: ticker.text
                    validator: RegularExpressionValidator { regularExpression: /^\d{0,20}(\.\d{0,18})?$/ }
                }
                BdxTextArea {
                    id: meta
                    label: "Metadata (optional)"
                    placeholderText: "website, description or JSON — up to 4096 characters, public"
                    minHeight: 70
                }
                BdxButton {
                    text: page.w.busy ? page.w.busyText : "Review registration"
                    variant: "primary"
                    Layout.fillWidth: true
                    enabled: ticker.text.length > 0 && fullName.text.length > 0 && maxSupply.text.length > 0
                             && page.w.synchronized && page.w.connected && !page.w.busy && !page.w.watchOnly
                    onClicked: page.w.registerToken({ ticker: ticker.text, fullName: fullName.text, decimals: decimals.text,
                                                      maxSupply: maxSupply.text, supply: supply.text, metaInfo: meta.text })
                }
                Mono {
                    text: "Costs 100 BDX on testnet (50 burned, 50 to governance) plus the network fee, and locks another 100 BDX as collateral for about 6 months (518,400 blocks), so 200+ unlocked BDX are needed. Minting burns 5 BDX, updating 1 BDX, burning is free. The ticker, name, decimals and maximum supply are fixed once registered; this wallet becomes the owner and alone can mint more or change the metadata."
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // --- sweep_all bdx:/token:/address ---------------------------------------------------
            Panel {
                title: "Sweep"
                Layout.fillWidth: true
                Caption { text: "What" }
                Flow {
                    Layout.fillWidth: true
                    spacing: 0
                    ChipGroup {
                        current: page.sweepWhat
                        model: [{ value: "bdx", label: "BDX only" }, { value: "all", label: "BDX + all tokens" }]
                               .concat(page.w.tokens.map(t => ({ value: t.tokenId, label: t.ticker })))
                        onPicked: v => page.sweepWhat = v
                    }
                }
                BdxTextField {
                    id: sweepTo
                    label: "To address"
                    placeholderText: "empty = this account (consolidates outputs)"
                    monoSmall: true
                    error: text.trim().length === 0 || WalletManager.addressValid(text.trim(), page.w.netType) ? "" : "Not a valid testnet address"
                }
                BdxButton {
                    text: "Review sweep"
                    variant: "primary"
                    Layout.fillWidth: true
                    enabled: page.w.synchronized && page.w.connected && !page.w.busy && sweepTo.error.length === 0
                    onClicked: page.w.sweep(page.sweepWhat, sweepTo.text.trim(), false)
                }
            }
        }
    }

    // --- tokens_by_owner and the whole registry ---------------------------------------------------
    Panel {
        title: "Token registry"
        note: page.results.length >= 100 ? "first 100 matches" : page.results.length + " match" + (page.results.length === 1 ? "" : "es")
        Layout.fillWidth: true
        contentPadding: 0
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.pad
            spacing: 12
            BdxTextField {
                label: "Search"
                placeholderText: "ticker, name or token id"
                Layout.fillWidth: true
                field.onTextChanged: page.search = field.text
            }
            BdxTextField {
                id: ownerField
                label: "Owner"
                placeholderText: "address or spend public key (blank = anyone)"
                monoSmall: true
                Layout.fillWidth: true
                error: page.ownerBad ? "Not a testnet address or 64-hex key" : ""
                field.onTextChanged: page.ownerFilter = field.text
                trailing: BdxButton { compact: true; text: "Me"; onClicked: ownerField.text = page.w.primaryAddress }
            }
        }
        TableHeader { cols: [["Token", -1], ["Supply", 220], ["Max supply", 220], ["Owner", 170]] }
        Repeater {
            model: page.results
            delegate: TableRow {
                id: hit
                required property var modelData
                clickable: true
                onClicked: infoDialog.show(hit.modelData)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Mono { text: hit.modelData.ticker; color: Theme.green; font.bold: true }
                    Mono { text: hit.modelData.fullName; color: Theme.textDim; elide: Text.ElideRight; Layout.fillWidth: true }
                }
                Mono { text: hit.modelData.supply; horizontalAlignment: Text.AlignRight; elide: Text.ElideLeft; Layout.preferredWidth: 220 }
                Mono { text: hit.modelData.maxSupply; color: Theme.textDim; horizontalAlignment: Text.AlignRight; elide: Text.ElideLeft; Layout.preferredWidth: 220 }
                Mono {
                    text: page.isMine(hit.modelData.owner) ? "you" : page.shortKey(hit.modelData.owner)
                    color: page.isMine(hit.modelData.owner) ? Theme.blue : Theme.textDim
                    Layout.preferredWidth: 170
                }
            }
        }
        Mono {
            visible: page.results.length === 0
            text: page.reg && page.reg.loading ? "Reading the token registry from the node (" + page.reg.loaded + " / " + page.reg.total + ")…"
                : page.reg && page.reg.count === 0 ? "The registry hasn't been read yet. It loads after the wallet syncs, or press Refresh tokens."
                : "No token matches."
            color: Theme.textFaint
            font.italic: true
            Layout.margins: Theme.pad
        }
    }

    // --- mint / burn / update -----------------------------------------------------------------------
    BdxDialog {
        id: actionDialog
        property string mode: "mint"
        property var tok: ({})
        title: mode === "mint" ? "Mint " + (tok.ticker || "")
             : mode === "burn" ? "Burn " + (tok.ticker || "") : "Update " + (tok.ticker || "")
        function show(m, id) {
            mode = m
            const r = page.reg.get(id)
            tok = Object.assign({}, page.w.tokenMeta(id), r)
            amount.text = ""
            metaEdit.text = r.metaInfo || ""
            newOwner.text = ""
            open()
        }

        KeyValue { key: "Token"; keyWidth: 110; value: (actionDialog.tok.fullName || "") + "  ·  " + actionDialog.tok.tokenId; }
        KeyValue {
            key: "Supply"; keyWidth: 110
            value: (actionDialog.tok.supply || "?") + " of " + (actionDialog.tok.maxSupply || "?")
            visible: actionDialog.mode === "mint"
        }
        BdxTextField {
            id: amount
            visible: actionDialog.mode !== "update"
            label: actionDialog.mode === "mint" ? "Amount to mint (up to " + (actionDialog.tok.mintable || "?") + ")" : "Amount to burn"
            suffix: actionDialog.tok.ticker || ""
            validator: RegularExpressionValidator { regularExpression: /^\d{0,20}(\.\d{0,18})?$/ }
        }
        BdxTextArea {
            id: metaEdit
            visible: actionDialog.mode === "update"
            label: "Metadata"
            minHeight: 80
        }
        BdxTextField {
            id: newOwner
            visible: actionDialog.mode === "update"
            label: "Transfer ownership to (optional)"
            placeholderText: "testnet address of the new owner"
            monoSmall: true
            error: text.trim().length === 0 || WalletManager.addressValid(text.trim(), page.w.netType) ? "" : "Not a valid testnet address"
        }
        Mono {
            text: actionDialog.mode === "burn" ? "Burning removes tokens from circulation for good."
                : actionDialog.mode === "update" ? "Handing over ownership is final: the new owner alone can then mint or update."
                : "Minted tokens are sent to this wallet."
            color: actionDialog.mode === "mint" ? Theme.textFaint : Theme.amber
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        footer: [
            Item { Layout.fillWidth: true },
            BdxButton { text: "Cancel"; onClicked: actionDialog.close() },
            BdxButton {
                text: page.w.busy ? page.w.busyText : "Review"
                variant: "primary"
                enabled: !page.w.busy && page.w.synchronized
                         && (actionDialog.mode === "update" ? newOwner.error.length === 0 : amount.text.length > 0)
                onClicked: {
                    const id = actionDialog.tok.tokenId
                    if (actionDialog.mode === "mint") page.w.mintToken(id, amount.text)
                    else if (actionDialog.mode === "burn") page.w.burnToken(id, amount.text)
                    else page.w.updateToken(id, metaEdit.text, newOwner.text.trim())
                    actionDialog.close()
                }
            }
        ]
    }

    // --- registry entry -----------------------------------------------------------------------------
    BdxDialog {
        id: infoDialog
        property var tok: ({})
        title: (tok.ticker || "") + " · " + (tok.fullName || "")
        function show(t) { tok = t; open() }
        KeyValue { key: "Supply"; keyWidth: 110; value: (infoDialog.tok.supply || "") + " " + (infoDialog.tok.ticker || "") }
        KeyValue { key: "Max supply"; keyWidth: 110; value: (infoDialog.tok.maxSupply || "") + " " + (infoDialog.tok.ticker || "") }
        KeyValue { key: "Decimals"; keyWidth: 110; value: String(infoDialog.tok.decimals) }
        KeyValue { key: "Metadata"; keyWidth: 110; value: infoDialog.tok.metaInfo || ""; visible: !!infoDialog.tok.metaInfo }
        CopyField { label: "Token ID"; value: infoDialog.tok.tokenId || "" }
        CopyField { label: "Owner (spend public key)" + (page.isMine(infoDialog.tok.owner || "") ? " · you" : ""); value: infoDialog.tok.owner || "" }
        footer: [
            Item { Layout.fillWidth: true },
            BdxButton { text: "Close"; onClicked: infoDialog.close() }
        ]
    }
}
