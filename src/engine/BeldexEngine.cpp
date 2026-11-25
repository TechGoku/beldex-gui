// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
//
// Production engine: adapts Beldex core's public wallet API (namespace ::Wallet,
// src/wallet/api/wallet2_api.h) to bdx::IEngine / bdx::IWallet.

#include "EngineCommon.h"
#include "WalletEngine.h"

#include <wallet2_api.h>

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QMutex>

#include <algorithm>
#include <set>

namespace bdx {
namespace {

using BWallet = ::Wallet::Wallet;
using BManager = ::Wallet::WalletManagerBase;

inline std::string s(const QString &q) { return q.toStdString(); }
inline QString q(const std::string &s) { return QString::fromStdString(s); }

::Wallet::NetworkType toNet(NetType n)
{
    switch (n) {
    case NetType::Testnet: return ::Wallet::TESTNET;
    case NetType::Devnet: return ::Wallet::DEVNET;
    default: return ::Wallet::MAINNET;
    }
}

NetType fromNet(::Wallet::NetworkType n)
{
    switch (n) {
    case ::Wallet::TESTNET: return NetType::Testnet;
    case ::Wallet::DEVNET: return NetType::Devnet;
    default: return NetType::Mainnet;
    }
}

// ---------------------------------------------------------------------------------------------
class BeldexPendingTx final : public IPendingTx {
public:
    BeldexPendingTx(BWallet *w, ::Wallet::PendingTransaction *tx) : m_wallet(w), m_tx(tx) {}
    ~BeldexPendingTx() override
    {
        if (m_tx)
            m_wallet->disposeTransaction(m_tx);
    }
    bool ok() const override { return m_tx && m_tx->good(); }
    QString errorString() const override { return m_tx ? q(m_tx->status().second) : QStringLiteral("no transaction"); }
    quint64 amount() const override { return m_tx ? m_tx->amount() : 0; }
    quint64 fee() const override { return m_tx ? m_tx->fee() : 0; }
    quint64 txCount() const override { return m_tx ? m_tx->txCount() : 0; }
    QStringList txids() const override
    {
        QStringList out;
        if (m_tx)
            for (const auto &id : m_tx->txid())
                out << q(id);
        return out;
    }
    bool commit(bool flash) override { return m_tx && m_tx->commit("", false, flash); }

private:
    BWallet *m_wallet;
    ::Wallet::PendingTransaction *m_tx;
};

// Bridges ::Wallet::WalletListener (called on the wallet's refresh thread) to IWalletListener.
class ListenerBridge final : public ::Wallet::WalletListener {
public:
    void setTarget(IWalletListener *t)
    {
        QMutexLocker lock(&m_mutex);
        m_target = t;
    }
    void moneySpent(const std::string &txId, uint64_t amount) override { call([&](auto *t) { t->moneySpent(q(txId), amount); }); }
    void moneyReceived(const std::string &txId, uint64_t amount) override { call([&](auto *t) { t->moneyReceived(q(txId), amount); }); }
    void unconfirmedMoneyReceived(const std::string &txId, uint64_t amount) override { call([&](auto *t) { t->unconfirmedMoneyReceived(q(txId), amount); }); }
    void newBlock(uint64_t height) override { call([&](auto *t) { t->newBlock(height); }); }
    void updated() override { call([](auto *t) { t->updated(); }); }
    void refreshed() override { call([](auto *t) { t->refreshed(); }); }

private:
    template <typename F> void call(F &&f)
    {
        QMutexLocker lock(&m_mutex);
        if (m_target)
            f(m_target);
    }
    QMutex m_mutex;
    IWalletListener *m_target = nullptr;
};

// ---------------------------------------------------------------------------------------------
class BeldexWallet final : public IWallet {
public:
    explicit BeldexWallet(BWallet *w) : m_w(w) { m_w->setListener(&m_listener); }
    ~BeldexWallet() override { m_w->setListener(nullptr); }

    BWallet *raw() const { return m_w; }

    bool ok() const override { return m_w->good(); }
    QString errorString() const override { return q(m_w->status().second); }

    QString seed() const override { return q(m_w->seed()); }
    QString seedLanguage() const override { return q(m_w->getSeedLanguage()); }
    QString address(quint32 a, quint32 i) const override { return q(m_w->address(a, i)); }
    QString path() const override { return q(m_w->path()); }
    NetType netType() const override { return fromNet(m_w->nettype()); }
    QString secretViewKey() const override { return q(m_w->secretViewKey()); }
    QString publicViewKey() const override { return q(m_w->publicViewKey()); }
    QString secretSpendKey() const override { return q(m_w->secretSpendKey()); }
    QString publicSpendKey() const override { return q(m_w->publicSpendKey()); }
    bool watchOnly() const override { return m_w->watchOnly(); }
    bool setPassword(const QString &pw) override { return m_w->setPassword(s(pw)); }
    bool store() override { return m_w->store(""); }

    bool init(const QString &daemon, const QString &user, const QString &pass, bool trusted) override
    {
        const bool r = m_w->init(s(daemon), 0, s(user), s(pass), false, false);
        m_w->setTrustedDaemon(trusted);
        return r;
    }
    ConnStatus connectionStatus() const override
    {
        switch (m_w->connected()) {
        case BWallet::ConnectionStatus_Connected: return ConnStatus::Connected;
        case BWallet::ConnectionStatus_WrongVersion: return ConnStatus::WrongVersion;
        default: return ConnStatus::Disconnected;
        }
    }
    void setRefreshFromHeight(quint64 h) override { m_w->setRefreshFromBlockHeight(h); }
    void setRecovering(bool r) override { m_w->setRecoveringFromSeed(r); }
    bool setAttribute(const QString &k, const QString &v) override { return m_w->setCacheAttribute(s(k), s(v)); }
    QString attribute(const QString &k) const override { return q(m_w->getCacheAttribute(s(k))); }
    quint64 refreshFromHeight() const override { return m_w->getRefreshFromBlockHeight(); }
    void startRefresh() override { m_w->startRefresh(); }
    void pauseRefresh() override { m_w->pauseRefresh(); }
    void refreshAsync() override { m_w->refreshAsync(); }
    void rescanAsync() override { m_w->rescanBlockchainAsync(); }
    bool synchronized() const override { return m_w->synchronized(); }
    bool isRefreshing() override { return m_w->isRefreshing(std::chrono::milliseconds{20}); }
    void stopScan() override { m_w->stop(); }
    quint64 blockHeight() const override { return m_w->blockChainHeight(); }
    quint64 daemonHeight() const override { return m_w->daemonBlockChainHeight(); }
    quint64 daemonTargetHeight() const override { return m_w->daemonBlockChainTargetHeight(); }
    std::optional<int> hardForkVersion() const override
    {
        if (auto v = m_w->hardForkVersion())
            return int(*v);
        return std::nullopt;
    }
    void setListener(IWalletListener *l) override { m_listener.setTarget(l); }

    quint64 balance(quint32 a) const override { return m_w->balance(a); }
    quint64 unlockedBalance(quint32 a) const override { return m_w->unlockedBalance(a); }

    std::vector<AccountRecord> accounts() override
    {
        std::vector<AccountRecord> out;
        const auto n = m_w->numSubaddressAccounts();
        for (quint32 i = 0; i < n; ++i)
            out.push_back({i, q(m_w->address(i, 0)), q(m_w->getSubaddressLabel(i, 0)), m_w->balance(i), m_w->unlockedBalance(i)});
        return out;
    }
    void addAccount(const QString &label) override { m_w->addSubaddressAccount(s(label)); }
    void setAccountLabel(quint32 a, const QString &label) override { m_w->setSubaddressLabel(a, 0, s(label)); }

    std::vector<SubaddressRecord> subaddresses(quint32 a) override
    {
        std::vector<SubaddressRecord> out;
        const auto n = m_w->numSubaddresses(a);
        for (quint32 i = 0; i < n; ++i)
            out.push_back({i, q(m_w->address(a, i)), q(m_w->getSubaddressLabel(a, i))});
        return out;
    }
    void addSubaddress(quint32 a, const QString &label) override { m_w->addSubaddress(a, s(label)); }
    void setSubaddressLabel(quint32 a, quint32 i, const QString &label) override { m_w->setSubaddressLabel(a, i, s(label)); }

    std::vector<TxRecord> history() override
    {
        std::vector<TxRecord> out;
        auto *h = m_w->history();
        h->refresh();
        for (const auto *t : h->getAll()) {
            TxRecord r;
            r.hash = q(t->hash());
            r.incoming = t->direction() == ::Wallet::TransactionInfo::Direction_In;
            r.pending = t->isPending();
            r.failed = t->isFailed();
            if (t->isStake()) r.kind = TxRecord::Kind::Stake;
            else if (t->isBns()) r.kind = TxRecord::Kind::Bns;
            else if (t->isMasterNodeReward()) r.kind = TxRecord::Kind::MasterNodeReward;
            else if (t->isMinerReward()) r.kind = TxRecord::Kind::MinerReward;
            r.amount = t->amount();
            r.fee = t->fee();
            r.blockHeight = t->blockHeight();
            r.confirmations = t->confirmations();
            r.unlockTime = t->unlockTime();
            r.timestamp = QDateTime::fromSecsSinceEpoch(qint64(t->timestamp()));
            r.paymentId = q(t->paymentId());
            r.description = q(t->description());
            r.label = q(t->label());
            r.account = t->subaddrAccount();
            for (auto i : t->subaddrIndex())
                r.subaddrIndices << i;
            for (const auto &tr : t->transfers())
                r.destinations << q(tr.address);
            out.push_back(std::move(r));
        }
        return out;
    }
    bool setTxNote(const QString &txid, const QString &note) override { return m_w->setUserNote(s(txid), s(note)); }
    QString txKey(const QString &txid) const override { return q(m_w->getTxKey(s(txid))); }

    std::vector<AddressBookRecord> addressBook() override
    {
        std::vector<AddressBookRecord> out;
        auto *ab = m_w->addressBook();
        ab->refresh();
        for (const auto *row : ab->getAll())
            out.push_back({int(row->getRowId()), q(row->getAddress()), q(row->getDescription())});
        return out;
    }
    bool addAddressBookRow(const QString &a, const QString &d) override { return m_w->addressBook()->addRow(s(a), s(d)); }
    bool deleteAddressBookRow(int rowId) override { return m_w->addressBook()->deleteRow(std::size_t(rowId)); }

    std::unique_ptr<IPendingTx> createTransaction(const QStringList &dests, const QList<quint64> &amounts,
                                                  uint32_t priority, quint32 account) override
    {
        std::vector<std::string> d;
        for (const auto &a : dests)
            d.push_back(s(a));
        std::optional<std::vector<uint64_t>> amt;
        if (!amounts.isEmpty())
            amt = std::vector<uint64_t>(amounts.begin(), amounts.end());
        return wrap(m_w->createTransactionMultDest(d, amt, std::nullopt /* BDX */, priority, account, {}));
    }
    std::unique_ptr<IPendingTx> createSweepAll(uint32_t priority, quint32 account) override
    {
        // "BDX": native coins only (no token id would sweep every privacy token too); no address: to ourselves.
        return wrap(m_w->createSweepAllTransaction(std::string("BDX"), std::nullopt, priority, account, {}));
    }
    quint64 estimateFee(uint32_t priority, quint32 recipients) const override
    {
        return m_w->estimateTransactionFee(priority, recipients);
    }

    std::vector<StakeRecord> stakes() override
    {
        std::vector<StakeRecord> out;
        std::unique_ptr<std::vector<::Wallet::stakeInfo>> list{m_w->listCurrentStakes()};
        if (list)
            for (const auto &si : *list)
                out.push_back({q(si.mn_pubkey), si.stake, si.unlock_height, si.awaiting, si.decommissioned});
        return out;
    }
    std::unique_ptr<IPendingTx> stake(const QString &mnKey, quint64 amount) override
    {
        return wrap(m_w->stakePending(s(mnKey), amount));
    }
    StakeUnlockResult canRequestStakeUnlock(const QString &mnKey) override
    {
        return unlockResult(m_w->canRequestStakeUnlock(s(mnKey)), false);
    }
    StakeUnlockResult requestStakeUnlock(const QString &mnKey) override
    {
        return unlockResult(m_w->requestStakeUnlock(s(mnKey)), true);
    }

    std::vector<BnsRecord> bnsRecords() override
    {
        std::vector<BnsRecord> out;
        std::unique_ptr<std::vector<::Wallet::bnsInfo>> list{m_w->MyBns()};
        if (list)
            for (const auto &b : *list)
                out.push_back({q(b.name), q(b.name_hash), q(b.owner), q(b.backup_owner), q(b.value_bchat),
                               q(b.value_wallet), q(b.value_belnet), q(b.value_eth_addr), b.update_height,
                               b.expiration_height});
        return out;
    }
    std::unique_ptr<IPendingTx> buyBns(const BnsRequest &r, quint32 account) override
    {
        std::string owner = s(r.owner), backup = s(r.backupOwner), years = s(r.years), bchat = s(r.bchat),
                    wallet = s(r.wallet), belnet = s(r.belnet), eth = s(r.eth), name = s(r.name);
        return wrap(m_w->createBnsTransaction(owner, backup, years, bchat, wallet, belnet, eth, name, 0, account, {}));
    }
    std::unique_ptr<IPendingTx> renewBns(const QString &n, const QString &y, quint32 account) override
    {
        std::string name = s(n), years = s(y);
        return wrap(m_w->bnsRenewTransaction(name, years, 0, account, {}));
    }
    std::unique_ptr<IPendingTx> updateBns(const BnsRequest &r, quint32 account) override
    {
        std::string owner = s(r.owner), backup = s(r.backupOwner), bchat = s(r.bchat), wallet = s(r.wallet),
                    belnet = s(r.belnet), eth = s(r.eth), name = s(r.name);
        return wrap(m_w->bnsUpdateTransaction(owner, backup, bchat, wallet, belnet, eth, name, 0, account, {}));
    }

    QString signMessage(const QString &m) override { return q(m_w->signMessage(s(m))); }
    bool verifySignedMessage(const QString &m, const QString &a, const QString &sig) const override
    {
        return m_w->verifySignedMessage(s(m), s(a), s(sig));
    }

private:
    std::unique_ptr<IPendingTx> wrap(::Wallet::PendingTransaction *tx)
    {
        return std::make_unique<BeldexPendingTx>(m_w, tx);
    }
    StakeUnlockResult unlockResult(::Wallet::StakeUnlockResult *r, bool withTx)
    {
        StakeUnlockResult out;
        if (!r)
            return out;
        out.success = r->success();
        out.message = q(r->msg());
        if (withTx && out.success)
            out.tx = wrap(r->ptx());  // ptx() allocates a new PendingTransaction we own
        // NOTE: ::Wallet::StakeUnlockResult has no virtual destructor in wallet2_api.h, so deleting
        // through the base pointer is undefined behaviour. The object is intentionally leaked (a few
        // bytes per unlock request) until upstream adds `virtual ~StakeUnlockResult() = default;`.
        return out;
    }

    BWallet *m_w;
    ListenerBridge m_listener;
};

// ---------------------------------------------------------------------------------------------
class BeldexEngine final : public IEngine {
public:
    BeldexEngine()
    {
        // Send Beldex core's log to a file instead of the terminal (as monero-gui does). It must be
        // initialised before the wallet manager, or core logs to stderr with its own defaults.
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        m_logPath = QDir(dir).filePath(QStringLiteral("beldex-wallet-gui.log"));
        const QByteArray argv0 = QCoreApplication::applicationFilePath().toLocal8Bit();
        BWallet::init(argv0.constData(), "beldex-wallet-gui", s(m_logPath), false);
        // BELDEX_GUI_LOG_LEVEL=0..4 raises core logging (e.g. 1 logs "Refresh done, blocks received").
        bool ok = false;
        const int level = qEnvironmentVariableIntValue("BELDEX_GUI_LOG_LEVEL", &ok);
        ::Wallet::WalletManagerFactory::setLogLevel(ok ? std::clamp(level, 0, 4) : 0);
        m_mgr = ::Wallet::WalletManagerFactory::getWalletManager();
    }

    QString name() const override { return QStringLiteral("Beldex wallet_api"); }
    QString logPath() const override { return m_logPath; }
    bool isDemo() const override { return false; }
    QString errorString() const override { return m_error; }

    bool walletExists(const QString &path) override { return m_mgr->walletExists(s(path)); }

    std::unique_ptr<IWallet> createWallet(const QString &path, const QString &pw, const QString &lang, NetType net) override
    {
        return adopt(m_mgr->createWallet(s(path), s(pw), s(lang), toNet(net)));
    }
    std::unique_ptr<IWallet> openWallet(const QString &path, const QString &pw, NetType net) override
    {
        return adopt(m_mgr->openWallet(s(path), s(pw), toNet(net)));
    }
    std::unique_ptr<IWallet> restoreFromSeed(const QString &path, const QString &pw, const QString &seed, NetType net,
                                             quint64 height, const QString &offset) override
    {
        return adopt(m_mgr->recoveryWallet(s(path), s(pw), s(seed.simplified()), toNet(net), height, 1, s(offset)));
    }
    std::unique_ptr<IWallet> restoreFromKeys(const QString &path, const QString &pw, NetType net, quint64 height,
                                             const QString &addr, const QString &view, const QString &spend) override
    {
        return adopt(m_mgr->createWalletFromKeys(s(path), s(pw), "English", toNet(net), height, s(addr), s(view), s(spend)));
    }
    bool closeWallet(std::unique_ptr<IWallet> w, bool store) override
    {
        if (!w)
            return true;
        auto *bw = static_cast<BeldexWallet *>(w.get())->raw();
        w->setListener(nullptr);
        w.reset();  // detaches the listener bridge before the wallet goes away
        return m_mgr->closeWallet(bw, store);
    }
    bool verifyPassword(const QString &keysFile, const QString &pw) override
    {
        return m_mgr->verifyWalletPassword(s(keysFile), s(pw), false);
    }
    quint64 estimateHeightForDate(NetType net, const QDate &d) const override { return estimateHeightFromDate(net, d); }

    QString displayAmount(quint64 a) const override { return q(BWallet::displayAmount(a)); }
    std::optional<quint64> amountFromString(const QString &a) const override
    {
        const QString t = a.trimmed();
        if (t.isEmpty())
            return std::nullopt;
        const auto v = BWallet::amountFromString(s(t));
        if (v == 0 && t.toDouble() != 0.0)
            return std::nullopt;
        return v;
    }
    bool addressValid(const QString &a, NetType net) const override { return BWallet::addressValid(s(a), toNet(net)); }
    bool masterNodeKeyValid(const QString &k) const override { return BWallet::masterNodePubkeyValid(s(k)); }

private:
    std::unique_ptr<IWallet> adopt(BWallet *w)
    {
        if (!w) {
            m_error = q(m_mgr->errorString());
            return nullptr;
        }
        if (!w->good()) {
            m_error = q(w->status().second);
            m_mgr->closeWallet(w, false);
            return nullptr;
        }
        m_error.clear();
        return std::make_unique<BeldexWallet>(w);
    }

    BManager *m_mgr = nullptr;
    QString m_error;
    QString m_logPath;
};

} // namespace

std::unique_ptr<IEngine> createEngine() { return std::make_unique<BeldexEngine>(); }

} // namespace bdx
