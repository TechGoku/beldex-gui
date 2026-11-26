// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "engine/WalletEngine.h"
#include "model/TransactionHistoryModel.h"

#include <QFuture>
#include <QObject>
#include <QQmlEngine>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QTimer>
#include <QSet>
#include <QVariant>

#include <functional>
#include <memory>
#include <optional>

class PendingTransaction;
class TokenRegistry;
class WalletListenerProxy;

// QML-facing wrapper around one open bdx::IWallet.
//
// Threading: every call into the wallet library runs on a private single-thread pool (m_worker),
// so calls are serialised and the GUI thread never blocks on disk or network. Results come back
// to the GUI thread and update cached properties, which QML binds to.
class Wallet : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use WalletManager.wallet")

    // identity
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(int netType READ netType CONSTANT)
    Q_PROPERTY(QString netName READ netName CONSTANT)
    Q_PROPERTY(bool watchOnly READ watchOnly CONSTANT)
    Q_PROPERTY(QString primaryAddress READ primaryAddress CONSTANT)

    // accounts / balance
    Q_PROPERTY(int accountIndex READ accountIndex WRITE setAccountIndex NOTIFY accountIndexChanged)
    Q_PROPERTY(QString address READ address NOTIFY accountIndexChanged)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(QString balance READ balance NOTIFY balanceChanged)
    Q_PROPERTY(QString unlockedBalance READ unlockedBalance NOTIFY balanceChanged)
    Q_PROPERTY(QString lockedBalance READ lockedBalance NOTIFY balanceChanged)
    Q_PROPERTY(QString totalBalance READ totalBalance NOTIFY balanceChanged)      // all accounts
    Q_PROPERTY(QString stakedBalance READ stakedBalance NOTIFY stakesChanged)

    // sync / daemon
    Q_PROPERTY(QString daemonAddress READ daemonAddress NOTIFY connectionChanged)
    Q_PROPERTY(int connectionStatus READ connectionStatus NOTIFY connectionChanged)  // 0 down, 1 up, 2 wrong version
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(double blockHeight READ blockHeight NOTIFY syncChanged)
    Q_PROPERTY(double daemonHeight READ daemonHeight NOTIFY syncChanged)
    Q_PROPERTY(double restoreHeight READ restoreHeight NOTIFY syncChanged)
    Q_PROPERTY(double syncProgress READ syncProgress NOTIFY syncChanged)            // 0..1
    Q_PROPERTY(bool synchronized READ synchronized NOTIFY syncChanged)
    Q_PROPERTY(int hardFork READ hardFork NOTIFY syncChanged)

    // collections
    Q_PROPERTY(TransactionHistoryModel *history READ history CONSTANT)
    Q_PROPERTY(QVariantList recentTransactions READ recentTransactions NOTIFY historyChanged)
    Q_PROPERTY(QVariantList subaddresses READ subaddresses NOTIFY subaddressesChanged)
    Q_PROPERTY(QVariantList addressBook READ addressBook NOTIFY addressBookChanged)
    Q_PROPERTY(QVariantList stakes READ stakes NOTIFY stakesChanged)
    Q_PROPERTY(QVariantList bnsRecords READ bnsRecords NOTIFY bnsChanged)

    // privacy tokens: testnet only (mainnet has no tokens yet)
    Q_PROPERTY(bool tokensEnabled READ tokensEnabled CONSTANT)
    Q_PROPERTY(QString spendPublicKey READ spendPublicKey CONSTANT)          // token ownership is by this key
    Q_PROPERTY(TokenRegistry *tokenRegistry READ tokenRegistry CONSTANT)
    Q_PROPERTY(QVariantList tokens READ tokens NOTIFY tokensChanged)          // balances of the current account
    Q_PROPERTY(QVariantList ownedTokens READ ownedTokens NOTIFY ownedTokensChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)

public:
    static constexpr const char *kRestoreHeightAttr = "beldex-gui.restore-height";
    static constexpr const char *kRestorePendingAttr = "beldex-gui.restore-pending";

    Wallet(bdx::IEngine *engine, std::unique_ptr<bdx::IWallet> wallet, QObject *parent = nullptr);
    ~Wallet() override;

    // Takes the wallet back out (for WalletManager::closeWallet). Waits for queued work first.
    std::unique_ptr<bdx::IWallet> release();
    void beginRelease();                               // GUI thread, non-blocking
    std::unique_ptr<bdx::IWallet> finishRelease();     // any thread, waits for queued jobs

    QString path() const { return m_path; }
    QString name() const;
    int netType() const { return int(m_net); }
    QString netName() const;
    bool watchOnly() const { return m_watchOnly; }
    QString primaryAddress() const { return m_primaryAddress; }

    int accountIndex() const { return m_account; }
    void setAccountIndex(int i);
    QString address() const;
    QVariantList accounts() const { return m_accounts; }
    QString balance() const;
    QString unlockedBalance() const;
    QString lockedBalance() const;
    QString totalBalance() const;
    QString stakedBalance() const;

    QString daemonAddress() const { return m_daemonAddress; }
    int connectionStatus() const { return int(m_conn); }
    bool connected() const { return m_conn == bdx::ConnStatus::Connected; }
    double blockHeight() const { return double(m_height); }
    double daemonHeight() const { return double(m_daemonHeight); }
    double restoreHeight() const { return double(m_restoreHeight); }
    double syncProgress() const;
    bool synchronized() const { return m_synced; }
    int hardFork() const { return m_hardFork; }

    TransactionHistoryModel *history() const { return m_history; }
    QVariantList recentTransactions() const;
    QVariantList subaddresses() const { return m_subaddresses; }
    QVariantList addressBook() const { return m_addressBook; }
    QVariantList stakes() const { return m_stakes; }
    QVariantList bnsRecords() const { return m_bns; }

    bool tokensEnabled() const { return m_net == bdx::NetType::Testnet; }
    QString spendPublicKey() const { return m_spendKey; }
    TokenRegistry *tokenRegistry() const { return m_registry; }
    QVariantList tokens() const { return m_tokens; }
    QVariantList ownedTokens() const { return m_ownedTokens; }

    bool busy() const { return m_busyCount > 0; }
    QString busyText() const { return m_busyText; }

    // --- daemon ----------------------------------------------------------------------------
    Q_INVOKABLE void connectToDaemon(const QString &address, const QString &user = {}, const QString &password = {},
                                     bool trusted = false);
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE void rescanBlockchain();
    // Forget everything scanned and scan again starting at `height` (0 = the whole chain).
    Q_INVOKABLE void rescanBlockchainFrom(double height);
    Q_INVOKABLE void store();

    // --- receive ---------------------------------------------------------------------------
    Q_INVOKABLE void addSubaddress(const QString &label);
    Q_INVOKABLE void setSubaddressLabel(int index, const QString &label);
    Q_INVOKABLE void addAccount(const QString &label);
    Q_INVOKABLE void setAccountLabel(int index, const QString &label);
    Q_INVOKABLE QString makePaymentUri(const QString &address, const QString &amount, const QString &description) const;

    // --- send ------------------------------------------------------------------------------
    // destinations: [{address: "bx…", amount: "12.5"}]. Emits transactionCreated(tx) or error(msg).
    // tokenId: send that privacy token instead of BDX (amounts in the token's units).
    Q_INVOKABLE void createTransaction(const QVariantList &destinations, bool flash, const QString &tokenId = QString());
    Q_INVOKABLE void createSweepAll(bool flash);
    // what: a token id, "bdx" or "all" (BDX and every token); address empty = this account.
    Q_INVOKABLE void sweep(const QString &what, const QString &address, bool flash);
    Q_INVOKABLE void estimateFee(int recipients, bool flash);   // emits feeEstimated
    Q_INVOKABLE void commitTransaction(PendingTransaction *tx);  // emits transactionCommitted
    Q_INVOKABLE void discardTransaction(PendingTransaction *tx);

    // --- history / address book ------------------------------------------------------------
    Q_INVOKABLE void setTxNote(const QString &txid, const QString &note);
    Q_INVOKABLE QString txKey(const QString &txid);
    Q_INVOKABLE bool addAddressBookEntry(const QString &address, const QString &description);
    Q_INVOKABLE void deleteAddressBookEntry(int rowId);

    // --- master node staking ---------------------------------------------------------------
    Q_INVOKABLE void stake(const QString &masterNodeKey, const QString &amount);
    Q_INVOKABLE void requestStakeUnlock(const QString &masterNodeKey);

    // --- BNS -------------------------------------------------------------------------------
    // fields: name, years ("1y"…), owner, backupOwner, bchat, wallet, belnet, eth
    Q_INVOKABLE void buyBns(const QVariantMap &fields);
    Q_INVOKABLE void renewBns(const QString &name, const QString &years);
    Q_INVOKABLE void updateBns(const QVariantMap &fields);

    // --- privacy tokens (testnet) ----------------------------------------------------------
    // fields: ticker, fullName, maxSupply, supply, decimals, metaInfo (supplies in whole tokens, e.g. "1000000.5")
    Q_INVOKABLE void registerToken(const QVariantMap &fields);
    Q_INVOKABLE void mintToken(const QString &tokenId, const QString &amount);
    Q_INVOKABLE void burnToken(const QString &tokenId, const QString &amount);
    // newOwner: address that takes over the token (empty keeps this wallet)
    Q_INVOKABLE void updateToken(const QString &tokenId, const QString &metaInfo, const QString &newOwner);
    Q_INVOKABLE QVariantMap tokenMeta(const QString &tokenId) const;   // {ticker, fullName, decimals} (decimals -1 = unknown)
    Q_INVOKABLE bool tokenAmountValid(const QString &amount, const QString &tokenId) const;
    // Owner filter for the registry: an address becomes its public spend key; a 64-hex key passes through.
    Q_INVOKABLE QString ownerKeyOf(const QString &addressOrKey) const;
    Q_INVOKABLE void refreshTokens();   // registry (from the node) and balances

    // --- tools / secrets (password-gated) --------------------------------------------------
    Q_INVOKABLE bool checkPassword(const QString &password);
    Q_INVOKABLE QVariantMap secrets(const QString &password);  // {seed, secretViewKey, ...} or {} if wrong
    Q_INVOKABLE bool changePassword(const QString &oldPassword, const QString &newPassword);
    Q_INVOKABLE QString signMessage(const QString &message);
    Q_INVOKABLE bool verifyMessage(const QString &message, const QString &address, const QString &signature);

    // called (queued) from WalletListenerProxy
    void onListenerUpdated(bool historyMayChange, quint64 scanHeight = 0);

signals:
    void accountIndexChanged();
    void accountsChanged();
    void balanceChanged();
    void connectionChanged();
    void syncChanged();
    void historyChanged();
    void subaddressesChanged();
    void addressBookChanged();
    void stakesChanged();
    void bnsChanged();
    void tokensChanged();
    void ownedTokensChanged();
    void busyChanged();

    void moneyReceived(const QString &txid, const QString &amount);
    void moneySpent(const QString &txid, const QString &amount);
    void synchronizedChanged(bool synchronized);

    void transactionCreated(PendingTransaction *tx);
    void transactionCommitted(bool success, const QString &message, const QStringList &txids);
    void feeEstimated(const QString &fee);
    void error(const QString &message);
    void info(const QString &message);

private:
    template <typename R> void run(const QString &busyText, std::function<R(bdx::IWallet &)> work,
                                   std::function<void(R)> done);
    void run(std::function<void(bdx::IWallet &)> work, std::function<void()> done = {});
    template <typename R> R runSync(std::function<R(bdx::IWallet &)> work);
    void beginBusy(const QString &text);
    void endBusy();

    enum class Reload { Balances, History, Subaddresses, AddressBook, Stakes, Bns };
    bool beginReload(Reload r);
    void endReload(Reload r);
    void flushDeferredReloads();
    void snapshotDuringScan();
    void applyAccounts(const std::vector<bdx::AccountRecord> &accounts);
    void applyHistory(std::vector<bdx::TxRecord> txs);
    void applySubaddresses(const std::vector<bdx::SubaddressRecord> &subs);
    void applyAddressBook(const std::vector<bdx::AddressBookRecord> &rows);
    void act(std::function<void(bdx::IWallet &)> change, int refreshMask);
    void pollStatus();
    void reloadBalances();
    void reloadHistory();
    void reloadSubaddresses();
    void reloadAddressBook();
    void reloadStakes();
    void reloadBns();
    void emitCreated(std::unique_ptr<bdx::IPendingTx> tx, const QString &kind, const QStringList &dests, bool flash,
                     const QString &note, const QString &tokenId = {}, const QString &unit = {},
                     const QString &amountText = {});
    void applyTokenBalances(const std::vector<bdx::TokenBalance> &balances);
    void updateTokenMeta();
    void loadDemoTokens();
    void syncTokenRegistry();
    std::optional<int> tokenDecimals(const QString &tokenId) const;
    void tokenOp(const QString &busy, std::function<std::unique_ptr<bdx::IPendingTx>(bdx::IWallet &, QString *)> work,
                 const QString &kind, const QString &tokenId, const QStringList &dests, const QString &unit,
                 const QString &amountText, const QString &note);

    bdx::IEngine *m_engine;
    std::unique_ptr<bdx::IWallet> m_w;
    std::unique_ptr<WalletListenerProxy> m_listener;
    QThreadPool m_worker;
    QTimer m_poll;
    bool m_pollInFlight = false;
    int m_pollTick = 0;
    int m_reloadInFlight = 0, m_reloadDirty = 0;
    bool m_scanning = false;
    bool m_releasing = false;
    bool m_snapshotInFlight = false;
    QElapsedTimer m_snapshotClock;  // core refresh thread is scanning (its lock blocks most wallet calls)

    QString m_path, m_primaryAddress, m_daemonAddress, m_busyText;
    bdx::NetType m_net;
    bool m_watchOnly = false;
    int m_account = 0;
    int m_busyCount = 0;

    QVariantList m_accounts, m_subaddresses, m_addressBook, m_stakes, m_bns;
    QString m_spendKey;
    TokenRegistry *m_registry = nullptr;
    std::vector<bdx::TokenBalance> m_tokenBalances;
    QVariantList m_tokens, m_ownedTokens;
    QSet<QString> m_metaRequested;   // token ids already asked of the node for their ticker/decimals
    quint64 m_balance = 0, m_unlocked = 0, m_totalBalance = 0, m_staked = 0;
    bdx::ConnStatus m_conn = bdx::ConnStatus::Disconnected;
    quint64 m_height = 0, m_daemonHeight = 0, m_restoreHeight = 0, m_syncStartHeight = 0;
    bool m_synced = false;
    int m_hardFork = 0;
    TransactionHistoryModel *m_history;
};
