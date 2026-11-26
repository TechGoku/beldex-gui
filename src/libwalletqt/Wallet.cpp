// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#include "Wallet.h"

#include "Amount.h"
#include "PendingTransaction.h"
#include "TokenRegistry.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QtConcurrent>

#include <atomic>
#include <limits>
#include <stdexcept>

using namespace bdx;

// ---------------------------------------------------------------------------------------------
// Receives callbacks on the wallet library's refresh thread and forwards them, coalesced, to the
// GUI thread. newBlock fires thousands of times per second while syncing, so at most one
// "updated" event is in flight at a time.
class WalletListenerProxy final : public IWalletListener {
public:
    explicit WalletListenerProxy(Wallet *w) : m_wallet(w) {}
    void detach() { m_wallet = nullptr; }

    void moneySpent(const QString &txid, quint64 amount) override
    {
        post([txid, amount](Wallet *w) {
            emit w->moneySpent(txid, formatAtomic(amount, 9));
            w->onListenerUpdated(true);
        });
    }
    void moneyReceived(const QString &txid, quint64 amount) override
    {
        post([txid, amount](Wallet *w) {
            emit w->moneyReceived(txid, formatAtomic(amount, 9));
            w->onListenerUpdated(true);
        });
    }
    void unconfirmedMoneyReceived(const QString &txid, quint64 amount) override { moneyReceived(txid, amount); }
    // Core's cached height only moves when a scan finishes; newBlock carries the live scan height.
    void newBlock(quint64 height) override
    {
        m_scanHeight = height;
        coalesced(false);
    }
    void updated() override { coalesced(true); }
    void refreshed() override { coalesced(true); }

private:
    void coalesced(bool history)
    {
        if (history)
            m_historyDirty = true;
        if (m_pending.exchange(true))
            return;
        post([this](Wallet *w) {
            m_pending = false;
            w->onListenerUpdated(m_historyDirty.exchange(false), m_scanHeight.load());
        });
    }
    template <typename F> void post(F &&f)
    {
        QPointer<Wallet> w = m_wallet.load();
        if (!w)
            return;
        QMetaObject::invokeMethod(
            w.data(), [w, f = std::forward<F>(f)]() mutable { if (w) f(w.data()); }, Qt::QueuedConnection);
    }
    std::atomic<Wallet *> m_wallet;
    std::atomic<bool> m_pending{false}, m_historyDirty{false};
    std::atomic<quint64> m_scanHeight{0};
};

// ---------------------------------------------------------------------------------------------
Wallet::Wallet(IEngine *engine, std::unique_ptr<IWallet> wallet, QObject *parent)
    : QObject(parent), m_engine(engine), m_w(std::move(wallet)), m_listener(std::make_unique<WalletListenerProxy>(this)),
      m_history(new TransactionHistoryModel(this))
{
    m_worker.setMaxThreadCount(1);
    m_worker.setExpiryTimeout(-1);

    m_path = m_w->path();
    m_net = m_w->netType();
    m_watchOnly = m_w->watchOnly();
    m_primaryAddress = m_w->address(0, 0);
    m_restoreHeight = m_w->refreshFromHeight();
    m_spendKey = m_w->publicSpendKey().toLower();
    m_w->setListener(m_listener.get());

    m_registry = new TokenRegistry(int(m_net), this);
    if (tokensEnabled()) {
        connect(m_registry, &TokenRegistry::changed, this, [this] { applyTokenBalances(m_tokenBalances); });
        if (engine->isDemo()) {
            m_registry->setOffline(true);
            loadDemoTokens();
        } else {
            m_registry->loadCache();
        }
    }

    m_poll.setInterval(engine->isDemo() ? 700 : 2000);
    connect(&m_poll, &QTimer::timeout, this, &Wallet::pollStatus);
    m_poll.start();

    reloadBalances();
    reloadSubaddresses();
    reloadAddressBook();
    reloadHistory();
    // Stakes and BNS records are queried from the node; they load once the wallet has synced.
}

Wallet::~Wallet()
{
    m_poll.stop();
    m_listener->detach();
    m_worker.waitForDone();
    if (m_w)
        m_w->setListener(nullptr);
}

std::unique_ptr<IWallet> Wallet::release()
{
    beginRelease();
    return finishRelease();
}

// GUI thread, never blocks: stop polling and interrupt a running scan; no new jobs start after this.
void Wallet::beginRelease()
{
    m_releasing = true;
    m_poll.stop();
    m_listener->detach();
    if (m_w) {
        // Both calls are lock-free. Stopping the scan releases core's refresh lock within one batch, so
        // queued jobs finish and closing saves the progress made so far instead of waiting for the scan.
        m_w->pauseRefresh();
        m_w->stopScan();
    }
}

// Any thread (WalletManager runs it off the GUI thread): wait for queued jobs, hand the wallet back.
std::unique_ptr<IWallet> Wallet::finishRelease()
{
    m_worker.waitForDone();
    if (m_w)
        m_w->setListener(nullptr);
    return std::move(m_w);
}

// --- async plumbing --------------------------------------------------------------------------

// Beldex's wallet_api lets some wallet2 exceptions escape (e.g. tools::error::no_connection_to_daemon
// from estimateTransactionFee / hardForkVersion / listCurrentStakes). An exception that leaves a
// QtConcurrent task is rethrown as QUnhandledException on the GUI thread and aborts the app, so every
// call into the wallet goes through guarded(): it returns a default value plus the error text.
template <typename R> using Guarded = std::pair<R, QString>;

// Core holds its refresh lock for a whole scan, so a wallet call made during a scan waits for the scan to
// end. For things the user asked for, pause the scan, do the call, and resume it.
template <typename R, typename F> static R withScanPaused(F &work, IWallet &w)
{
    const bool scanning = w.isRefreshing();
    if (scanning) {
        w.pauseRefresh();
        w.stopScan();
    }
    struct Resume {
        IWallet &w;
        bool on;
        ~Resume() { if (on) w.startRefresh(); }
    } resume{w, scanning};
    return work(w);
}

template <typename R, typename F> static Guarded<R> guarded(F &work, IWallet &w, bool pauseScan = false)
{
    try {
        if (pauseScan)
            return {withScanPaused<R>(work, w), QString()};
        return {work(w), QString()};
    } catch (const std::exception &e) {
        return {R{}, QString::fromLocal8Bit(e.what())};
    } catch (...) {
        return {R{}, QStringLiteral("unknown error")};
    }
}

static QString friendlyError(const QString &raw)
{
    if (raw.contains(QLatin1String("connection to daemon"), Qt::CaseInsensitive) || raw.contains(QLatin1String("no_connection_to_daemon")) || raw.contains(QLatin1String("daemon_busy")))
        return QObject::tr("No connection to the node. Check Settings › Node.");
    return raw;
}

template <typename R>
void Wallet::run(const QString &busyText, std::function<R(IWallet &)> work, std::function<void(R)> done)
{
    if (m_releasing || !m_w)
        return;
    if (!busyText.isEmpty())
        beginBusy(busyText);
    IWallet *w = m_w.get();
    const bool userAction = !busyText.isEmpty();
    QFuture<Guarded<R>> fut = QtConcurrent::run(&m_worker, [w, userAction, work = std::move(work)]() mutable {
        return guarded<R>(work, *w, userAction);
    });
    auto *watcher = new QFutureWatcher<Guarded<R>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, fut, busyText, done = std::move(done)]() mutable {
        watcher->deleteLater();
        if (!busyText.isEmpty())
            endBusy();
        Guarded<R> r = fut.takeResult();
        if (!r.second.isEmpty()) {
            qWarning("wallet: %s", qPrintable(r.second));
            if (!busyText.isEmpty())      // user-initiated: tell them; background polls stay quiet
                emit error(explainTxError(r.second));
        }
        if (done)
            done(std::move(r.first));
    });
    watcher->setFuture(fut);
}

// A user edit (label, subaddress, contact, note…): pause a running scan, apply the change, save, and read
// back the affected data in the same job, so the UI updates at once even mid-scan (normal reloads are
// deferred while scanning).
void Wallet::act(std::function<void(IWallet &)> change, int refreshMask)
{
    if (m_releasing || !m_w)
        return;
    struct Fresh {
        std::vector<AccountRecord> accounts;
        std::vector<SubaddressRecord> subs;
        std::vector<AddressBookRecord> book;
        std::vector<TxRecord> txs;
    };
    const quint32 acct = quint32(m_account);
    IWallet *w = m_w.get();
    QFuture<Guarded<Fresh>> fut = QtConcurrent::run(&m_worker, [w, acct, refreshMask, change = std::move(change)]() mutable {
        auto job = [&](IWallet &x) {
            change(x);
            x.store();
            Fresh f;
            if (refreshMask & (1 << int(Reload::Balances))) f.accounts = x.accounts();
            if (refreshMask & (1 << int(Reload::Subaddresses))) f.subs = x.subaddresses(acct);
            if (refreshMask & (1 << int(Reload::AddressBook))) f.book = x.addressBook();
            if (refreshMask & (1 << int(Reload::History))) f.txs = x.history();
            return f;
        };
        return guarded<Fresh>(job, *w, true);
    });
    auto *watcher = new QFutureWatcher<Guarded<Fresh>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, fut, refreshMask]() mutable {
        watcher->deleteLater();
        Guarded<Fresh> r = fut.takeResult();
        if (!r.second.isEmpty()) {
            qWarning("wallet: %s", qPrintable(r.second));
            emit error(friendlyError(r.second));
            return;
        }
        Fresh &f = r.first;
        if (refreshMask & (1 << int(Reload::Balances))) applyAccounts(f.accounts);
        if (refreshMask & (1 << int(Reload::Subaddresses))) applySubaddresses(f.subs);
        if (refreshMask & (1 << int(Reload::AddressBook))) applyAddressBook(f.book);
        if (refreshMask & (1 << int(Reload::History))) applyHistory(std::move(f.txs));
    });
    watcher->setFuture(fut);
}

void Wallet::run(std::function<void(IWallet &)> work, std::function<void()> done)
{
    run<bool>(QString(), [work = std::move(work)](IWallet &w) { work(w); return true; },
              [done = std::move(done)](bool) { if (done) done(); });
}

template <typename R> R Wallet::runSync(std::function<R(IWallet &)> work)
{
    if (m_releasing || !m_w)
        return R{};
    IWallet *w = m_w.get();
    Guarded<R> r = QtConcurrent::run(&m_worker, [w, work = std::move(work)]() mutable { return guarded<R>(work, *w, true); }).takeResult();
    if (!r.second.isEmpty()) {
        qWarning("wallet: %s", qPrintable(r.second));
        emit error(friendlyError(r.second));
    }
    return std::move(r.first);
}

void Wallet::beginBusy(const QString &text)
{
    ++m_busyCount;
    m_busyText = text;
    emit busyChanged();
}

void Wallet::endBusy()
{
    m_busyCount = std::max(0, m_busyCount - 1);
    if (!m_busyCount)
        m_busyText.clear();
    emit busyChanged();
}

// --- identity --------------------------------------------------------------------------------

QString Wallet::name() const { return QFileInfo(m_path).completeBaseName(); }

QString Wallet::netName() const
{
    switch (m_net) {
    case NetType::Testnet: return QStringLiteral("TESTNET");
    case NetType::Devnet: return QStringLiteral("DEVNET");
    default: return QStringLiteral("MAINNET");
    }
}

void Wallet::setAccountIndex(int i)
{
    if (i == m_account || i < 0 || i >= m_accounts.size())
        return;
    m_account = i;
    emit accountIndexChanged();
    reloadBalances();
    reloadSubaddresses();
}

QString Wallet::address() const
{
    if (m_account < m_accounts.size())
        return m_accounts[m_account].toMap().value("address").toString();
    return m_primaryAddress;
}

QString Wallet::balance() const { return formatAtomic(m_balance, 4); }
QString Wallet::unlockedBalance() const { return formatAtomic(m_unlocked, 4); }
QString Wallet::lockedBalance() const { return formatAtomic(m_balance > m_unlocked ? m_balance - m_unlocked : 0, 4); }
QString Wallet::totalBalance() const { return formatAtomic(m_totalBalance, 4); }
QString Wallet::stakedBalance() const { return formatAtomic(m_staked, 4); }

double Wallet::syncProgress() const
{
    if (m_synced)
        return 1.0;
    if (m_daemonHeight == 0)
        return 0.0;
    const quint64 start = std::min(m_syncStartHeight, m_height);
    if (m_daemonHeight <= start)
        return 1.0;
    return std::clamp(double(m_height - start) / double(m_daemonHeight - start), 0.0, 1.0);
}

QVariantList Wallet::recentTransactions() const
{
    QVariantList out;
    const auto &all = m_history->all();
    for (size_t i = 0; i < all.size() && i < 8; ++i)
        out << m_history->toMap(all[i]);
    return out;
}

// --- daemon / sync ---------------------------------------------------------------------------

void Wallet::connectToDaemon(const QString &address, const QString &user, const QString &password, bool trusted)
{
    m_daemonAddress = address;
    if (!m_engine->isDemo())
        m_registry->setNode(address);
    m_conn = ConnStatus::Disconnected;
    emit connectionChanged();
    struct Initial {
        ConnStatus conn = ConnStatus::Disconnected;
        quint64 daemonHeight = 0;
        quint64 restoreHeight = 0;
        int hf = 0;
    };
    run<Initial>(tr("Connecting to %1…").arg(address),
                 [address, user, password, trusted](IWallet &w) {
                     Initial r;
                     r.restoreHeight = 0;
                     // A restore that never got to scan (closed or crashed first) must resume as a restore;
                     // otherwise core sees a never-synced wallet, calls it new and skips its whole history.
                     if (w.blockHeight() <= 1 && w.attribute(QString::fromLatin1(kRestorePendingAttr)) == QLatin1String("1")) {
                         w.setRecovering(true);
                         w.setRefreshFromHeight(w.attribute(QString::fromLatin1(kRestoreHeightAttr)).toULongLong());
                     }
                     w.init(address, user, password, trusted);
                     // Ask the node for its state now: once the refresh thread starts, these calls block
                     // until the whole scan is finished (see IWallet::isRefreshing).
                     r.conn = w.connectionStatus();
                     if (r.conn == ConnStatus::Connected) {
                         r.daemonHeight = w.daemonHeight();
                         try {
                             r.hf = w.hardForkVersion().value_or(0);
                         } catch (const std::exception &) {
                         }
                     }
                     r.restoreHeight = w.refreshFromHeight();
                     w.startRefresh();
                     return r;
                 },
                 [this](Initial r) {
                     m_conn = r.conn;
                     m_daemonHeight = r.daemonHeight;
                     m_hardFork = r.hf;
                     m_restoreHeight = r.restoreHeight;
                     emit connectionChanged();
                     emit syncChanged();
                     pollStatus();
                 });
}

void Wallet::pollStatus()
{
    if (m_pollInFlight || !m_w)
        return;
    m_pollInFlight = true;
    struct Status {
        bool scanning = false;  // core refresh thread busy: only local values were read
        ConnStatus conn = ConnStatus::Disconnected;
        quint64 height = 0, daemonHeight = 0, restoreHeight = 0;
        bool synced = false;
        int hf = 0;
    };
    run<Status>(QString(),
                [](IWallet &w) {
                    Status s;
                    s.scanning = w.isRefreshing();       // try-lock, never blocks
                    s.height = w.blockHeight();          // lock-free
                    s.restoreHeight = w.refreshFromHeight();  // lock-free
                    if (s.scanning)
                        return s;                        // anything else would wait for the scan to end
                    s.conn = w.connectionStatus();
                    if (s.conn == ConnStatus::Connected) {
                        s.daemonHeight = w.daemonHeight();
                        s.synced = w.synchronized() && s.daemonHeight > 0 && s.height + 1 >= s.daemonHeight;
                        try {
                            s.hf = w.hardForkVersion().value_or(0);
                        } catch (const std::exception &) {
                            s.hf = 0;  // node briefly unreachable; keep the rest of the status
                        }
                    }
                    return s;
                },
                [this](Status s) {
                    m_pollInFlight = false;
                    if (s.scanning) {
                        // Scanning means the node answered; keep the node values we already have, and the
                        // live scan height from newBlock (the cached height lags until the scan ends).
                        s.height = std::max(s.height, m_height);
                        s.conn = m_conn == ConnStatus::WrongVersion ? m_conn : ConnStatus::Connected;
                        s.daemonHeight = std::max(m_daemonHeight, s.height);
                        s.hf = m_hardFork;
                        s.synced = false;
                    }
                    const bool connChanged = s.conn != m_conn;
                    const bool heightChanged = s.height != m_height;
                    const bool syncFlip = s.synced != m_synced;
                    if (m_syncStartHeight == 0 || s.height < m_syncStartHeight)
                        m_syncStartHeight = s.height;
                    const bool scanEnded = m_scanning && !s.scanning;
                    m_scanning = s.scanning;
                    m_conn = s.conn;
                    m_height = s.height;
                    m_restoreHeight = s.restoreHeight;
                    m_daemonHeight = s.daemonHeight;
                    m_synced = s.synced;
                    m_hardFork = s.hf;
                    if (scanEnded)
                        flushDeferredReloads();
                    else if (m_scanning)
                        snapshotDuringScan();
                    if (connChanged)
                        emit connectionChanged();
                    emit syncChanged();
                    if (syncFlip)
                        emit synchronizedChanged(m_synced);
                    if (syncFlip && m_synced)
                        run([](IWallet &w) {
                            if (w.attribute(QString::fromLatin1(kRestorePendingAttr)) == QLatin1String("1")) {
                                w.setAttribute(QString::fromLatin1(kRestorePendingAttr), QStringLiteral("0"));
                                w.store();
                            }
                        });
                    if (heightChanged || syncFlip)
                        reloadBalances();
                    // History is expensive; while catching up refresh it only every ~8 polls.
                    if (syncFlip || (heightChanged && (m_synced || ++m_pollTick % 8 == 0))) {
                        reloadHistory();
                        if (syncFlip) {
                            reloadStakes();
                            reloadBns();
                            if (m_synced)
                                syncTokenRegistry();
                        }
                    }
                });
}

void Wallet::onListenerUpdated(bool historyMayChange, quint64 scanHeight)
{
    if (scanHeight > m_height) {
        m_height = scanHeight;
        if (m_daemonHeight < m_height)
            m_daemonHeight = m_height;
        emit syncChanged();
    }
    reloadBalances();
    if (historyMayChange) {
        reloadHistory();
        if (m_synced)
            reloadStakes();  // needs the node; while scanning it would block the worker until the scan ends
    }
}

void Wallet::refreshAll()
{
    run([](IWallet &w) { w.refreshAsync(); });
    if (tokensEnabled() && m_engine->isDemo())
        loadDemoTokens();
    reloadBalances();
    reloadHistory();
    reloadSubaddresses();
    reloadAddressBook();
    reloadStakes();
    reloadBns();
}

void Wallet::rescanBlockchain()
{
    m_synced = false;
    m_height = 0;
    m_syncStartHeight = m_restoreHeight;
    emit syncChanged();
    run([](IWallet &w) { w.rescanAsync(); }, [this] { emit info(tr("Rescan started")); });
}

void Wallet::rescanBlockchainFrom(double height)
{
    const quint64 h = quint64(std::max(0.0, height));
    m_synced = false;
    m_height = 0;               // progress comes back through newBlock as the rescan runs
    m_restoreHeight = h;
    m_syncStartHeight = h;
    emit syncChanged();
    emit synchronizedChanged(false);
    run([h](IWallet &w) {
            w.setRefreshFromHeight(h);
            w.setAttribute(QString::fromLatin1(kRestoreHeightAttr), QString::number(h));
            w.rescanAsync();
        },
        [this, h] { emit info(tr("Rescanning from block %1").arg(h)); });
}

void Wallet::store()
{
    run<bool>(tr("Saving wallet…"), [](IWallet &w) { return w.store(); },
              [this](bool ok) { if (!ok) emit error(tr("Could not save the wallet file")); });
}

// --- reloads ---------------------------------------------------------------------------------

// At most one reload of each kind is queued or running. Requests that arrive meanwhile set a dirty flag
// and trigger exactly one more run. Without this, scan progress events (many per second during a long
// restore) queued thousands of reloads; their QFutureWatchers piled up and made shutdown take minutes.
bool Wallet::beginReload(Reload r)
{
    const int bit = 1 << int(r);
    if ((m_reloadInFlight & bit) || m_scanning) {
        m_reloadDirty |= bit;   // while scanning: core's lock would block the worker; run when the scan ends
        return false;
    }
    if (!m_w)
        return false;
    m_reloadInFlight |= bit;
    return true;
}

void Wallet::flushDeferredReloads()
{
    const int dirty = m_reloadDirty;
    m_reloadDirty = 0;
    for (int r = 0; r <= int(Reload::Bns); ++r)
        if (dirty & (1 << r))
            switch (Reload(r)) {
            case Reload::Balances: reloadBalances(); break;
            case Reload::History: reloadHistory(); break;
            case Reload::Subaddresses: reloadSubaddresses(); break;
            case Reload::AddressBook: reloadAddressBook(); break;
            case Reload::Stakes: reloadStakes(); break;
            case Reload::Bns: reloadBns(); break;
            }
}

void Wallet::endReload(Reload r)
{
    const int bit = 1 << int(r);
    m_reloadInFlight &= ~bit;
    if ((m_reloadDirty & bit) && !m_scanning) {
        m_reloadDirty &= ~bit;
        QMetaObject::invokeMethod(this, [this, r] {
            switch (r) {
            case Reload::Balances: reloadBalances(); break;
            case Reload::History: reloadHistory(); break;
            case Reload::Subaddresses: reloadSubaddresses(); break;
            case Reload::AddressBook: reloadAddressBook(); break;
            case Reload::Stakes: reloadStakes(); break;
            case Reload::Bns: reloadBns(); break;
            }
        }, Qt::QueuedConnection);
    }
}

void Wallet::reloadBalances()
{
    if (!beginReload(Reload::Balances))
        return;
    struct B {
        std::vector<AccountRecord> accounts;
        std::vector<TokenBalance> tokens;
    };
    const bool tokens = tokensEnabled();
    const quint32 acct = quint32(m_account);
    run<B>(QString(),
           [tokens, acct](IWallet &w) {
               B b{w.accounts(), {}};
               if (tokens)
                   b.tokens = w.tokenBalances(acct);
               return b;
           },
           [this, tokens](B b) {
               endReload(Reload::Balances);
               applyAccounts(b.accounts);
               if (tokens && !b.accounts.empty())
                   applyTokenBalances(b.tokens);
           });
}

void Wallet::applyAccounts(const std::vector<AccountRecord> &accounts)
{
    if (accounts.empty())
        return;  // a wallet always has account #0; empty means the read failed, so keep what is shown
    QVariantList accs;
    quint64 total = 0;
    for (const auto &a : accounts) {
        accs << QVariantMap{{"index", a.index}, {"address", a.address}, {"label", a.label},
                            {"balance", formatAtomic(a.balance, 4)}, {"unlocked", formatAtomic(a.unlocked, 4)}};
        total += a.balance;
    }
    const bool accChanged = accs != m_accounts;
    m_accounts = accs;
    if (m_account >= int(accounts.size()))
        m_account = 0;
    const AccountRecord &cur = accounts[size_t(m_account)];
    const bool balChanged = cur.balance != m_balance || cur.unlocked != m_unlocked || total != m_totalBalance;
    m_balance = cur.balance;
    m_unlocked = cur.unlocked;
    m_totalBalance = total;
    if (accChanged) {
        emit accountsChanged();
        emit accountIndexChanged();
    }
    if (balChanged)
        emit balanceChanged();
}

void Wallet::reloadHistory()
{
    if (!beginReload(Reload::History))
        return;
    run<std::vector<TxRecord>>(QString(), [](IWallet &w) { return w.history(); },
                               [this](std::vector<TxRecord> txs) {
                                   endReload(Reload::History);
                                   applyHistory(std::move(txs));
                               });
}

void Wallet::applyHistory(std::vector<TxRecord> txs)
{
    m_history->setTransactions(std::move(txs));
    if (tokensEnabled())
        updateTokenMeta();
    emit historyChanged();
}

// During a long scan normal reloads are deferred (core's lock would block them until the scan ends).
// Every 60 s, briefly pause the scan to show the balance and transactions found so far, then resume.
void Wallet::snapshotDuringScan()
{
    // Only once the scan is past the restore height: below it core just fetches block hashes (nothing of
    // ours can be there), and interrupting that fast phase makes it markedly slower.
    if (m_height < m_restoreHeight + 1000)
        return;
    if (m_snapshotInFlight || (m_snapshotClock.isValid() && m_snapshotClock.elapsed() < 60000))
        return;
    m_snapshotInFlight = true;
    m_snapshotClock.restart();
    struct Snap {
        std::vector<AccountRecord> accounts;
        std::vector<TxRecord> txs;
    };
    run<Snap>(QString(),
              [](IWallet &w) {
                  auto read = [](IWallet &x) { return Snap{x.accounts(), x.history()}; };
                  return withScanPaused<Snap>(read, w);
              },
              [this](Snap s) {
                  m_snapshotInFlight = false;
                  if (s.accounts.empty())
                      return;  // the call failed; keep what is shown
                  applyAccounts(s.accounts);
                  applyHistory(std::move(s.txs));
              });
}

void Wallet::reloadSubaddresses()
{
    if (!beginReload(Reload::Subaddresses))
        return;
    const quint32 acct = quint32(m_account);
    run<std::vector<SubaddressRecord>>(QString(), [acct](IWallet &w) { return w.subaddresses(acct); },
                                       [this](std::vector<SubaddressRecord> subs) {
                                           endReload(Reload::Subaddresses);
                                           applySubaddresses(subs);
                                       });
}

void Wallet::applySubaddresses(const std::vector<SubaddressRecord> &subs)
{
    if (subs.empty())
        return;  // index 0 always exists; empty means the read failed
    QVariantList out;
    for (const auto &s : subs)
        out << QVariantMap{{"index", s.index}, {"address", s.address}, {"label", s.label}};
    m_subaddresses = out;
    emit subaddressesChanged();
}

void Wallet::reloadAddressBook()
{
    if (!beginReload(Reload::AddressBook))
        return;
    run<std::vector<AddressBookRecord>>(QString(), [](IWallet &w) { return w.addressBook(); },
                                        [this](std::vector<AddressBookRecord> rows) {
                                            endReload(Reload::AddressBook);
                                            applyAddressBook(rows);
                                        });
}

void Wallet::applyAddressBook(const std::vector<AddressBookRecord> &rows)
{
    QVariantList out;
    for (const auto &r : rows)
        out << QVariantMap{{"rowId", r.rowId}, {"address", r.address}, {"description", r.description}};
    m_addressBook = out;
    emit addressBookChanged();
}

void Wallet::reloadStakes()
{
    if (!beginReload(Reload::Stakes))
        return;
    run<std::vector<StakeRecord>>(QString(), [](IWallet &w) { return w.stakes(); },
                                  [this](std::vector<StakeRecord> st) {
                                      endReload(Reload::Stakes);
                                      QVariantList out;
                                      quint64 total = 0;
                                      for (const auto &s : st) {
                                          total += s.amount;
                                          QString state = s.decommissioned ? QStringLiteral("decommissioned")
                                                          : s.awaiting     ? QStringLiteral("awaiting")
                                                          : s.unlockHeight ? QStringLiteral("unlocking")
                                                                           : QStringLiteral("active");
                                          out << QVariantMap{{"masterNodeKey", s.masterNodeKey},
                                                             {"amount", formatAtomic(s.amount, 4)},
                                                             {"unlockHeight", s.unlockHeight ? double(*s.unlockHeight) : 0.0},
                                                             {"state", state}};
                                      }
                                      m_stakes = out;
                                      m_staked = total;
                                      emit stakesChanged();
                                  });
}

void Wallet::reloadBns()
{
    if (!beginReload(Reload::Bns))
        return;
    run<std::vector<BnsRecord>>(QString(), [](IWallet &w) { return w.bnsRecords(); },
                                [this](std::vector<BnsRecord> recs) {
                                    endReload(Reload::Bns);
                                    QVariantList out;
                                    for (const auto &b : recs)
                                        out << QVariantMap{{"name", b.name}, {"nameHash", b.nameHash}, {"owner", b.owner},
                                                           {"backupOwner", b.backupOwner}, {"bchat", b.bchat},
                                                           {"wallet", b.wallet}, {"belnet", b.belnet}, {"eth", b.eth},
                                                           {"updateHeight", double(b.updateHeight)},
                                                           {"expirationHeight", double(b.expirationHeight)}};
                                    m_bns = out;
                                    emit bnsChanged();
                                });
}

// --- receive ---------------------------------------------------------------------------------

void Wallet::addSubaddress(const QString &label)
{
    const quint32 acct = quint32(m_account);
    act([acct, label](IWallet &w) { w.addSubaddress(acct, label); }, 1 << int(Reload::Subaddresses));
}

void Wallet::setSubaddressLabel(int index, const QString &label)
{
    const quint32 acct = quint32(m_account);
    act([acct, index, label](IWallet &w) { w.setSubaddressLabel(acct, quint32(index), label); },
        (1 << int(Reload::Subaddresses)) | (1 << int(Reload::Balances)));
}

void Wallet::addAccount(const QString &label)
{
    act([label](IWallet &w) { w.addAccount(label); }, 1 << int(Reload::Balances));
}

void Wallet::setAccountLabel(int index, const QString &label)
{
    act([index, label](IWallet &w) { w.setAccountLabel(quint32(index), label); }, 1 << int(Reload::Balances));
}

QString Wallet::makePaymentUri(const QString &address, const QString &amount, const QString &description) const
{
    QUrlQuery q;
    if (!amount.trimmed().isEmpty())
        q.addQueryItem(QStringLiteral("tx_amount"), amount.trimmed());
    if (!description.trimmed().isEmpty())
        q.addQueryItem(QStringLiteral("tx_description"), QString::fromLatin1(QUrl::toPercentEncoding(description.trimmed())));
    const QString query = q.toString(QUrl::FullyEncoded);
    return QStringLiteral("beldex:") + address + (query.isEmpty() ? QString() : QLatin1Char('?') + query);
}

// --- send ------------------------------------------------------------------------------------

// Core's "not enough money … available only 0" also happens when the funds are there but still locked: every
// transaction locks its change (and anything received) for 10 blocks. Say so when part of the balance is locked.
QString Wallet::explainTxError(const QString &raw) const
{
    QString msg = friendlyError(raw);
    if ((msg.contains(QLatin1String("not enough money"), Qt::CaseInsensitive) ||
         msg.contains(QLatin1String("not enough unlocked"), Qt::CaseInsensitive) ||
         msg.contains(QLatin1String("No unlocked balance"), Qt::CaseInsensitive)) &&
        m_balance > m_unlocked)
        msg += QStringLiteral("\n") +
               tr("%1 BDX of this account is still locked: change and received funds unlock 10 blocks (about 5 minutes) "
                  "after their transaction. Try again then.")
                   .arg(lockedBalance());
    return msg;
}

void Wallet::emitCreated(std::unique_ptr<IPendingTx> tx, const QString &kind, const QStringList &dests, bool flash,
                         const QString &note, const QString &tokenId, const QString &unit, const QString &amountText)
{
    if (!tx)
        return;  // the call threw; run() already reported it
    if (!tx->ok()) {
        emit error(explainTxError(tx->errorString()));
        return;
    }
    auto *p = new PendingTransaction(std::move(tx), kind, dests, flash, note, this);
    if (!unit.isNull())
        p->setToken(tokenId, unit, amountText);
    QQmlEngine::setObjectOwnership(p, QQmlEngine::CppOwnership);
    emit transactionCreated(p);
}

void Wallet::createTransaction(const QVariantList &destinations, bool flash, const QString &tokenId)
{
    std::optional<int> decimals;
    if (!tokenId.isEmpty()) {
        decimals = tokenDecimals(tokenId);
        if (!tokensEnabled() || !decimals) {
            emit error(tr("Unknown token %1").arg(tokenId.left(12) + QStringLiteral("…")));
            return;
        }
    }
    QStringList addrs;
    QList<quint64> amounts;
    for (const auto &v : destinations) {
        const auto m = v.toMap();
        const QString a = m.value("address").toString().trimmed();
        const auto amt = decimals ? bdx::parseUnits(m.value("amount").toString(), *decimals)
                                  : m_engine->amountFromString(m.value("amount").toString());
        if (!m_engine->addressValid(a, m_net)) {
            emit error(tr("Invalid %1 address: %2").arg(netName().toLower(), a.left(14) + QStringLiteral("…")));
            return;
        }
        if (!amt || *amt == 0) {
            emit error(tr("Invalid amount: %1").arg(m.value("amount").toString()));
            return;
        }
        addrs << a;
        amounts << *amt;
    }
    if (addrs.isEmpty()) {
        emit error(tr("Add at least one recipient"));
        return;
    }
    const quint32 acct = quint32(m_account);
    const uint32_t prio = flash ? PriorityFlash : PriorityDefault;
    QString unit, amountText;
    if (decimals) {
        quint64 sum = 0;
        for (auto a : amounts)
            sum += a;
        unit = tokenMeta(tokenId).value("ticker").toString();
        amountText = bdx::formatUnits(sum, *decimals, *decimals);
    }
    run<std::unique_ptr<IPendingTx>>(tr("Creating transaction…"),
                                     [addrs, amounts, prio, acct, tokenId](IWallet &w) {
                                         return w.createTransaction(addrs, amounts, prio, acct, tokenId);
                                     },
                                     [this, addrs, flash, tokenId, unit, amountText](std::unique_ptr<IPendingTx> tx) {
                                         emitCreated(std::move(tx), QStringLiteral("transfer"), addrs, flash, {}, tokenId,
                                                     unit, amountText);
                                     });
}

void Wallet::createSweepAll(bool flash)
{
    const quint32 acct = quint32(m_account);
    const uint32_t prio = flash ? PriorityFlash : PriorityDefault;
    run<std::unique_ptr<IPendingTx>>(tr("Creating sweep transaction…"),
                                     [prio, acct](IWallet &w) { return w.createSweepAll(prio, acct); },
                                     [this, flash](std::unique_ptr<IPendingTx> tx) {
                                         emitCreated(std::move(tx), QStringLiteral("sweep"), {m_primaryAddress}, flash,
                                                     tr("Sends the entire unlocked balance of this account to its primary address."));
                                     });
}

void Wallet::sweep(const QString &what, const QString &address, bool flash)
{
    const QString to = address.trimmed();
    if (!to.isEmpty() && !m_engine->addressValid(to, m_net)) {
        emit error(tr("Invalid %1 address: %2").arg(netName().toLower(), to.left(14) + QStringLiteral("…")));
        return;
    }
    const bool bdxOnly = what == QLatin1String("bdx");
    const bool all = what == QLatin1String("all");
    if (!bdxOnly && (!tokensEnabled() || (!all && !tokenDecimals(what)))) {
        emit error(tr("Unknown token %1").arg(what.left(12) + QStringLiteral("…")));
        return;
    }
    const quint32 acct = quint32(m_account);
    const uint32_t prio = flash ? PriorityFlash : PriorityDefault;
    const QString dest = to.isEmpty() ? this->address() : to;
    const QString where = to.isEmpty() ? tr("this account's address") : tr("the address above");
    QString unit, note;
    if (bdxOnly) {
        unit = QStringLiteral("BDX");
        note = tr("Sends the entire unlocked BDX balance of this account to %1. Tokens stay.").arg(where);
    } else if (all) {
        unit = QString(QLatin1String(""));  // mixed: BDX and tokens
        note = tr("Sends the entire unlocked BDX balance and every token of this account to %1.").arg(where);
    } else {
        unit = tokenMeta(what).value("ticker").toString();
        note = tr("Sends the entire unlocked %1 balance of this account to %2. The fee is paid in BDX.").arg(unit, where);
    }
    const int decimals = bdxOnly || all ? 0 : *tokenDecimals(what);
    run<std::unique_ptr<IPendingTx>>(tr("Creating sweep transaction…"),
                                     [what, to, prio, acct](IWallet &w) { return w.sweep(what, to, prio, acct); },
                                     [this, flash, dest, note, what, unit, bdxOnly, all, decimals](std::unique_ptr<IPendingTx> tx) {
                                         if (bdxOnly) {
                                             emitCreated(std::move(tx), QStringLiteral("sweep"), {dest}, flash, note);
                                             return;
                                         }
                                         const QString text = all ? tr("All BDX and tokens")
                                                                  : tx && tx->ok() ? bdx::formatUnits(tx->amount(), decimals, decimals)
                                                                                   : QString();
                                         emitCreated(std::move(tx), QStringLiteral("sweep"), {dest}, flash, note,
                                                     all ? QString() : what, unit, text);
                                     });
}

void Wallet::estimateFee(int recipients, bool flash)
{
    const uint32_t prio = flash ? PriorityFlash : PriorityDefault;
    run<quint64>(QString(), [prio, recipients](IWallet &w) { return w.estimateFee(prio, quint32(std::max(1, recipients))); },
                 [this](quint64 fee) { emit feeEstimated(formatAtomic(fee, 6)); });
}

void Wallet::commitTransaction(PendingTransaction *tx)
{
    if (!tx || !tx->raw())
        return;
    IPendingTx *raw = tx->raw();
    const bool flash = tx->flash();
    const QString tokenOpId = tx->kind().startsWith(QLatin1String("token-")) ? tx->tokenId() : QString();
    QPointer<PendingTransaction> guard(tx);
    struct Sent {
        bool ok = false;
        QString error;
        QStringList ids;
    };
    run<Sent>(tr("Broadcasting transaction…"),
                                  [raw, flash, tokenOpId](IWallet &w) {
                                      Sent r;
                                      r.ids = raw->txids();   // core empties the pending transaction on commit
                                      r.ok = raw->commit(flash);
                                      if (r.ok) {
                                          // History can't name the token of an update (it has no token outputs).
                                          for (const QString &id : std::as_const(r.ids))
                                              if (!tokenOpId.isEmpty())
                                                  w.setAttribute(QStringLiteral("beldex-gui.token-op.") + id, tokenOpId);
                                          w.store();
                                      } else {
                                          r.error = raw->errorString();
                                      }
                                      return r;
                                  },
                                  [this, guard, tokenOpId](Sent r) {
                                      const QStringList ids = r.ids;
                                      if (r.ok && !tokenOpId.isEmpty() && !m_engine->isDemo()) {
                                          // The registry changes once the transaction is in a block (~30 s each).
                                          QPointer<Wallet> self(this);
                                          for (int ms : {40000, 100000, 200000})
                                              QTimer::singleShot(ms, this, [self, tokenOpId] {
                                                  if (self)
                                                      self->m_registry->refreshToken(tokenOpId);
                                              });
                                      }
                                      emit transactionCommitted(r.ok, r.ok ? tr("Transaction sent") : r.error, ids);
                                      if (guard)
                                          guard->deleteLater();
                                      refreshAll();
                                  });
}

void Wallet::discardTransaction(PendingTransaction *tx)
{
    if (tx)
        tx->deleteLater();
}

// --- history / address book -------------------------------------------------------------------

void Wallet::setTxNote(const QString &txid, const QString &note)
{
    act([txid, note](IWallet &w) { w.setTxNote(txid, note); }, 1 << int(Reload::History));
}

QString Wallet::txKey(const QString &txid)
{
    return runSync<QString>([txid](IWallet &w) { return w.txKey(txid); });
}

bool Wallet::addAddressBookEntry(const QString &address, const QString &description)
{
    if (!m_engine->addressValid(address.trimmed(), m_net)) {
        emit error(tr("That is not a valid %1 address").arg(netName().toLower()));
        return false;
    }
    const QString a = address.trimmed();
    act([a, description](IWallet &w) {
            if (!w.addAddressBookRow(a, description))
                throw std::runtime_error("Could not add address book entry");
        },
        1 << int(Reload::AddressBook));
    return true;
}

void Wallet::deleteAddressBookEntry(int rowId)
{
    act([rowId](IWallet &w) { w.deleteAddressBookRow(rowId); }, 1 << int(Reload::AddressBook));
}

// --- staking ---------------------------------------------------------------------------------

void Wallet::stake(const QString &masterNodeKey, const QString &amount)
{
    const QString key = masterNodeKey.trimmed();
    if (!m_engine->masterNodeKeyValid(key)) {
        emit error(tr("Master node key must be 64 hex characters"));
        return;
    }
    const auto amt = m_engine->amountFromString(amount);
    if (!amt || *amt == 0) {
        emit error(tr("Enter the amount to stake"));
        return;
    }
    run<std::unique_ptr<IPendingTx>>(tr("Preparing stake…"), [key, a = *amt](IWallet &w) { return w.stake(key, a); },
                                     [this, key](std::unique_ptr<IPendingTx> tx) {
                                         emitCreated(std::move(tx), QStringLiteral("stake"), {key}, false,
                                                     tr("Staked BDX stay in your wallet but are locked until you request an unlock."));
                                     });
}

void Wallet::requestStakeUnlock(const QString &masterNodeKey)
{
    const QString key = masterNodeKey.trimmed();
    run<StakeUnlockResult>(tr("Checking unlock…"),
                           [key](IWallet &w) {
                               StakeUnlockResult can = w.canRequestStakeUnlock(key);
                               if (!can.success)
                                   return can;
                               StakeUnlockResult r = w.requestStakeUnlock(key);
                               if (r.message.isEmpty())
                                   r.message = can.message;
                               return r;
                           },
                           [this, key](StakeUnlockResult r) {
                               if (!r.success || !r.tx) {
                                   emit error(r.message.isEmpty() ? tr("Stake cannot be unlocked") : r.message);
                                   return;
                               }
                               emitCreated(std::move(r.tx), QStringLiteral("unlock"), {key}, false, r.message);
                           });
}

// --- BNS -------------------------------------------------------------------------------------

static BnsRequest bnsFromMap(const QVariantMap &m)
{
    BnsRequest r;
    r.name = m.value("name").toString().trimmed().toLower();
    if (!r.name.isEmpty() && !r.name.endsWith(QStringLiteral(".bdx")))
        r.name += QStringLiteral(".bdx");
    r.years = m.value("years").toString();
    r.owner = m.value("owner").toString().trimmed();
    r.backupOwner = m.value("backupOwner").toString().trimmed();
    r.bchat = m.value("bchat").toString().trimmed();
    r.wallet = m.value("wallet").toString().trimmed();
    r.belnet = m.value("belnet").toString().trimmed();
    r.eth = m.value("eth").toString().trimmed();
    return r;
}

void Wallet::buyBns(const QVariantMap &fields)
{
    const BnsRequest req = bnsFromMap(fields);
    const quint32 acct = quint32(m_account);
    run<std::unique_ptr<IPendingTx>>(tr("Preparing BNS purchase…"), [req, acct](IWallet &w) { return w.buyBns(req, acct); },
                                     [this, req](std::unique_ptr<IPendingTx> tx) {
                                         emitCreated(std::move(tx), QStringLiteral("bns-buy"), {req.name}, false,
                                                     tr("Registers %1 for %2. The BNS fee is burned.").arg(req.name, req.years));
                                     });
}

void Wallet::renewBns(const QString &name, const QString &years)
{
    const quint32 acct = quint32(m_account);
    run<std::unique_ptr<IPendingTx>>(tr("Preparing BNS renewal…"), [name, years, acct](IWallet &w) { return w.renewBns(name, years, acct); },
                                     [this, name, years](std::unique_ptr<IPendingTx> tx) {
                                         emitCreated(std::move(tx), QStringLiteral("bns-renew"), {name}, false,
                                                     tr("Extends %1 by %2.").arg(name, years));
                                     });
}

void Wallet::updateBns(const QVariantMap &fields)
{
    const BnsRequest req = bnsFromMap(fields);
    const quint32 acct = quint32(m_account);
    run<std::unique_ptr<IPendingTx>>(tr("Preparing BNS update…"), [req, acct](IWallet &w) { return w.updateBns(req, acct); },
                                     [this, req](std::unique_ptr<IPendingTx> tx) {
                                         emitCreated(std::move(tx), QStringLiteral("bns-update"), {req.name}, false,
                                                     tr("Updates the records of %1.").arg(req.name));
                                     });
}

// --- tools / secrets -------------------------------------------------------------------------

bool Wallet::checkPassword(const QString &password)
{
    return m_engine->verifyPassword(m_path + QStringLiteral(".keys"), password);
}

QVariantMap Wallet::secrets(const QString &password)
{
    if (!checkPassword(password))
        return {};
    return runSync<QVariantMap>([](IWallet &w) {
        return QVariantMap{{"seed", w.seed()},
                           {"seedLanguage", w.seedLanguage()},
                           {"secretViewKey", w.secretViewKey()},
                           {"publicViewKey", w.publicViewKey()},
                           {"secretSpendKey", w.secretSpendKey()},
                           {"publicSpendKey", w.publicSpendKey()},
                           {"restoreHeight", double(w.refreshFromHeight())}};
    });
}

bool Wallet::changePassword(const QString &oldPassword, const QString &newPassword)
{
    if (!checkPassword(oldPassword)) {
        emit error(tr("Current password is wrong"));
        return false;
    }
    const bool ok = runSync<bool>([newPassword](IWallet &w) { return w.setPassword(newPassword); });
    if (ok)
        emit info(tr("Password changed"));
    else
        emit error(tr("Could not change password"));
    return ok;
}

QString Wallet::signMessage(const QString &message)
{
    return runSync<QString>([message](IWallet &w) { return w.signMessage(message); });
}

bool Wallet::verifyMessage(const QString &message, const QString &address, const QString &signature)
{
    return runSync<bool>([=](IWallet &w) { return w.verifySignedMessage(message, address.trimmed(), signature.trimmed()); });
}

// --- privacy tokens --------------------------------------------------------------------------

namespace {
// Monero-style base58 (8-byte blocks -> 11 chars). Used only to read the spend key out of an address that
// core already validated, so the registry can be filtered by owner without asking about every token.
QByteArray base58Decode(const QString &text)
{
    static const QByteArray alphabet("123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz");
    static const int encodedSizes[] = {0, 2, 3, 5, 6, 7, 9, 10, 11};
    const QByteArray in = text.toLatin1();
    int lastBytes = -1;
    for (int i = 0; i <= 8; ++i)
        if (encodedSizes[i] == in.size() % 11)
            lastBytes = i;
    if (lastBytes < 0)
        return {};
    QByteArray out;
    auto block = [&](int pos, int len, int bytes) {
        quint64 v = 0;
        for (int i = 0; i < len; ++i) {
            const int d = alphabet.indexOf(in[pos + i]);
            if (d < 0 || v > (std::numeric_limits<quint64>::max() - quint64(d)) / 58)
                return false;
            v = v * 58 + quint64(d);
        }
        if (bytes < 8 && v >> (8 * bytes))
            return false;
        for (int i = bytes - 1; i >= 0; --i)
            out.append(char((v >> (8 * i)) & 0xff));
        return true;
    };
    int pos = 0;
    for (; pos + 11 <= in.size(); pos += 11)
        if (!block(pos, 11, 8))
            return {};
    if (lastBytes > 0 && !block(pos, in.size() - pos, lastBytes))
        return {};
    return out;
}
} // namespace

std::optional<int> Wallet::tokenDecimals(const QString &tokenId) const
{
    for (const auto &b : m_tokenBalances)
        if (b.tokenId == tokenId)
            return b.decimals;
    if (const auto t = m_registry->info(tokenId))
        return t->decimals;
    return std::nullopt;
}

QVariantMap Wallet::tokenMeta(const QString &tokenId) const
{
    QVariantMap m{{"tokenId", tokenId}, {"ticker", tokenId.left(8) + QStringLiteral("…")}, {"fullName", QString()},
                  {"decimals", -1}};
    if (const auto t = m_registry->info(tokenId)) {
        m["ticker"] = t->ticker;
        m["fullName"] = t->fullName;
        m["decimals"] = t->decimals;
        m["owner"] = t->owner;
        m["mine"] = t->owner.compare(m_spendKey, Qt::CaseInsensitive) == 0;
    }
    for (const auto &b : m_tokenBalances)
        if (b.tokenId == tokenId) {
            if (!b.ticker.isEmpty())
                m["ticker"] = b.ticker;
            m["decimals"] = b.decimals;
        }
    return m;
}

bool Wallet::tokenAmountValid(const QString &amount, const QString &tokenId) const
{
    const auto d = tokenDecimals(tokenId);
    return d && bdx::parseUnits(amount, *d).has_value();
}

QString Wallet::ownerKeyOf(const QString &addressOrKey) const
{
    const QString v = addressOrKey.trimmed();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{64}$"));
    if (hex.match(v).hasMatch())
        return v.toLower();
    if (v == m_primaryAddress)
        return m_spendKey;
    if (!m_engine->addressValid(v, m_net))
        return {};
    const QByteArray raw = base58Decode(v);
    int tag = 0;  // varint network prefix
    while (tag < raw.size() && (uchar(raw[tag]) & 0x80))
        ++tag;
    ++tag;
    if (raw.size() < tag + 64 + 4)
        return {};
    return QString::fromLatin1(raw.mid(tag, 32).toHex());
}

void Wallet::applyTokenBalances(const std::vector<TokenBalance> &balances)
{
    m_tokenBalances = balances;
    QVariantList out;
    for (const auto &b : balances) {
        const auto reg = m_registry->info(b.tokenId);
        // A token we hold but the registry doesn't know yet (registered since it was read, e.g. one we just
        // created): ask the node now, so owner actions (mint, update) work without waiting for the daily read.
        if (!reg && !m_metaRequested.contains(b.tokenId)) {
            m_metaRequested.insert(b.tokenId);
            m_registry->refreshToken(b.tokenId);
        }
        out << QVariantMap{{"tokenId", b.tokenId},
                           {"ticker", !b.ticker.isEmpty() ? b.ticker : reg ? reg->ticker : b.tokenId.left(8) + QStringLiteral("…")},
                           {"fullName", reg ? reg->fullName : QString()},
                           {"decimals", b.decimals},
                           {"balance", bdx::formatUnits(b.balance, b.decimals, 4)},
                           {"unlocked", bdx::formatUnits(b.unlocked, b.decimals, 4)},
                           {"unlockedFull", bdx::formatUnits(b.unlocked, b.decimals, b.decimals, false)},
                           {"locked", b.balance > b.unlocked},
                           {"mine", reg && reg->owner.compare(m_spendKey, Qt::CaseInsensitive) == 0}};
    }
    if (out != m_tokens) {
        m_tokens = out;
        emit tokensChanged();
    }
    updateTokenMeta();
}

// Tickers and decimals for history rows; "my tokens" from the registry. Token ids we have never seen are
// asked of the node once.
void Wallet::updateTokenMeta()
{
    QHash<QString, TransactionHistoryModel::TokenMeta> meta;
    for (const QString &id : m_history->tokenIds()) {
        const QVariantMap m = tokenMeta(id);
        meta.insert(id, {m.value("ticker").toString(), m.value("decimals").toInt()});
        if (m.value("decimals").toInt() < 0 && !m_metaRequested.contains(id)) {
            m_metaRequested.insert(id);
            m_registry->refreshToken(id);
        }
    }
    m_history->setTokenMeta(std::move(meta));

    QVariantList owned;
    for (const auto &t : m_registry->byOwner(m_spendKey)) {
        QVariantMap m = TokenRegistry::toMap(t);
        QString held = QStringLiteral("0");
        for (const auto &b : m_tokenBalances)
            if (b.tokenId == t.tokenId)
                held = bdx::formatUnits(b.balance, b.decimals, 4);
        m["held"] = held;
        owned << m;
    }
    if (owned != m_ownedTokens) {
        m_ownedTokens = owned;
        emit ownedTokensChanged();
    }
}

void Wallet::loadDemoTokens()
{
    run<std::vector<TokenInfo>>(QString(), [](IWallet &w) { return w.knownTokens(); },
                                [this](std::vector<TokenInfo> all) { m_registry->setAll(all); });
}

void Wallet::syncTokenRegistry()
{
    if (!tokensEnabled() || m_engine->isDemo())
        return;
    // Ticker, name and decimals never change; supply, metadata and owner can. A full read takes about a
    // minute on testnet, so it runs when the copy is missing or a day old; our own tokens refresh each time.
    const QDateTime updated = QDateTime::fromString(m_registry->updated(), QStringLiteral("yyyy-MM-dd HH:mm"));
    if (m_registry->count() == 0 || !updated.isValid() || updated.secsTo(QDateTime::currentDateTime()) > 24 * 3600) {
        m_registry->refresh();
        return;
    }
    for (const auto &t : m_registry->byOwner(m_spendKey))
        m_registry->refreshToken(t.tokenId);
}

void Wallet::refreshTokens()
{
    if (!tokensEnabled())
        return;
    if (m_engine->isDemo())
        loadDemoTokens();
    else
        m_registry->refresh();
    reloadBalances();
}

void Wallet::tokenOp(const QString &busy, std::function<std::unique_ptr<IPendingTx>(IWallet &, QString *)> work,
                     const QString &kind, const QString &tokenId, const QStringList &dests, const QString &unit,
                     const QString &amountText, const QString &note)
{
    if (!tokensEnabled()) {
        emit error(tr("Privacy tokens are only available on testnet"));
        return;
    }
    struct R {
        std::unique_ptr<IPendingTx> tx;
        QString id;
    };
    run<std::shared_ptr<R>>(busy,
                            [work](IWallet &w) {
                                auto r = std::make_shared<R>();
                                r->tx = work(w, &r->id);
                                return r;
                            },
                            [this, kind, tokenId, dests, unit, amountText, note](std::shared_ptr<R> r) {
                                if (!r)
                                    return;
                                const QString id = tokenId.isEmpty() ? r->id : tokenId;
                                emitCreated(std::move(r->tx), kind, dests, false,
                                            id.isEmpty() ? note : note + QStringLiteral("\n") + tr("Token id: %1").arg(id), id,
                                            unit, amountText);
                            });
}

void Wallet::registerToken(const QVariantMap &f)
{
    const QString ticker = f.value("ticker").toString().trimmed();
    const QString name = f.value("fullName").toString().trimmed();
    const QString meta = f.value("metaInfo").toString();
    bool decOk = false;
    const int decimals = f.value("decimals").toString().trimmed().toInt(&decOk);
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9]{1,14}$")).match(ticker).hasMatch())
        return emit error(tr("Ticker: 1–14 letters or digits"));
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9 _.\\-]{1,64}$")).match(name).hasMatch())
        return emit error(tr("Name: 1–64 letters, digits, spaces, '_', '-' or '.'"));
    if (!decOk || decimals < 0 || decimals > 18)
        return emit error(tr("Decimals must be 0–18"));
    const auto maxSupply = bdx::parseUnits(f.value("maxSupply").toString(), decimals);
    if (!maxSupply || *maxSupply == 0)
        return emit error(tr("Maximum supply must be above zero and fit %1 decimals (at most 18,446,744,073,709,551,615 units in total)").arg(decimals));
    const QString supplyText = f.value("supply").toString().trimmed();
    const auto supply = supplyText.isEmpty() ? std::optional<quint64>(0) : bdx::parseUnits(supplyText, decimals);
    if (!supply || *supply > *maxSupply)
        return emit error(tr("Initial supply must be between 0 and the maximum supply"));
    if (meta.toUtf8().size() > 4096)
        return emit error(tr("Metadata is limited to 4096 bytes"));
    // Supplies are uint64: written as JSON text so they never pass through a double.
    QJsonObject strings{{"ticker", ticker}, {"full_name", name}, {"meta_info", meta}};
    QString json = QString::fromUtf8(QJsonDocument(strings).toJson(QJsonDocument::Compact));
    json.insert(1, QStringLiteral("\"version\":1,\"total_max_supply\":%1,\"current_supply\":%2,\"decimal_point\":%3,")
                       .arg(*maxSupply).arg(*supply).arg(decimals));
    const quint32 acct = quint32(m_account);
    tokenOp(tr("Preparing token registration…"),
            [json, acct](IWallet &w, QString *id) { return w.registerToken(json, id, PriorityDefault, acct); },
            QStringLiteral("token-register"), {}, {m_primaryAddress}, ticker,
            bdx::formatUnits(*supply, decimals, decimals),
            tr("Registers %1 (%2): max supply %3, %4 decimals. The initial supply is sent to this wallet; only "
               "this wallet can mint more. Registering costs 100 BDX on testnet (50 burned, 50 to governance) on top "
               "of the network fee, and locks another 100 BDX of this wallet as collateral for 518,400 blocks "
               "(about 6 months); it comes back to this wallet after that.")
                .arg(ticker, name, bdx::formatUnits(*maxSupply, decimals, decimals))
                .arg(decimals));
}

void Wallet::mintToken(const QString &tokenId, const QString &amount)
{
    const auto t = m_registry->info(tokenId);
    if (!t)
        return emit error(tr("Unknown token %1").arg(tokenId.left(12) + QStringLiteral("…")));
    if (t->owner.compare(m_spendKey, Qt::CaseInsensitive) != 0)
        return emit error(tr("Only the owner of %1 can mint it").arg(t->ticker));
    const auto amt = bdx::parseUnits(amount, t->decimals);
    if (!amt || *amt == 0)
        return emit error(tr("Invalid amount: %1").arg(amount));
    if (*amt > t->maxSupply - std::min(t->supply, t->maxSupply))
        return emit error(tr("At most %1 %2 can still be minted")
                              .arg(bdx::formatUnits(t->maxSupply - std::min(t->supply, t->maxSupply), t->decimals, t->decimals), t->ticker));
    const quint32 acct = quint32(m_account);
    tokenOp(tr("Preparing mint…"),
            [tokenId, a = *amt, acct](IWallet &w, QString *) { return w.mintToken(tokenId, a, PriorityDefault, acct); },
            QStringLiteral("token-mint"), tokenId, {m_primaryAddress}, t->ticker, bdx::formatUnits(*amt, t->decimals, t->decimals),
            tr("New %1 are created and sent to this wallet. Minting burns 5 BDX on testnet.").arg(t->ticker));
}

void Wallet::burnToken(const QString &tokenId, const QString &amount)
{
    const auto d = tokenDecimals(tokenId);
    if (!d)
        return emit error(tr("Unknown token %1").arg(tokenId.left(12) + QStringLiteral("…")));
    const auto amt = bdx::parseUnits(amount, *d);
    if (!amt || *amt == 0)
        return emit error(tr("Invalid amount: %1").arg(amount));
    const QString ticker = tokenMeta(tokenId).value("ticker").toString();
    const quint32 acct = quint32(m_account);
    tokenOp(tr("Preparing burn…"),
            [tokenId, a = *amt, acct](IWallet &w, QString *) { return w.burnToken(tokenId, a, PriorityDefault, acct); },
            QStringLiteral("token-burn"), tokenId, {}, ticker, bdx::formatUnits(*amt, *d, *d),
            tr("Burned %1 are destroyed publicly and the token's supply goes down. This cannot be undone.").arg(ticker));
}

void Wallet::updateToken(const QString &tokenId, const QString &metaInfo, const QString &newOwner)
{
    const auto t = m_registry->info(tokenId);
    if (!t)
        return emit error(tr("Unknown token %1").arg(tokenId.left(12) + QStringLiteral("…")));
    if (t->owner.compare(m_spendKey, Qt::CaseInsensitive) != 0)
        return emit error(tr("Only the owner of %1 can update it").arg(t->ticker));
    const QString owner = newOwner.trimmed();
    if (!owner.isEmpty() && !m_engine->addressValid(owner, m_net))
        return emit error(tr("New owner must be a %1 address").arg(netName().toLower()));
    if (metaInfo.toUtf8().size() > 4096)
        return emit error(tr("Metadata is limited to 4096 bytes"));
    if (metaInfo == t->metaInfo && (owner.isEmpty() || owner == m_primaryAddress))
        return emit error(tr("Nothing to update: change the metadata or the owner"));
    QJsonObject o{{"meta_info", metaInfo}};
    if (!owner.isEmpty())
        o.insert("owner", owner);
    const QString json = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    const QString note = owner.isEmpty() || owner == m_primaryAddress
                             ? tr("Replaces the metadata of %1. Updating burns 1 BDX on testnet.").arg(t->ticker)
                             : tr("Hands %1 to another wallet: after this only that wallet can mint or update it. "
                                  "Updating burns 1 BDX on testnet.").arg(t->ticker);
    tokenOp(tr("Preparing token update…"),
            [tokenId, json](IWallet &w, QString *) { return w.updateToken(tokenId, json, PriorityDefault); },
            QStringLiteral("token-update"), tokenId, owner.isEmpty() ? QStringList{} : QStringList{owner}, t->ticker, QString(),
            note);
}
