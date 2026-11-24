// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#include "Wallet.h"

#include "Amount.h"
#include "PendingTransaction.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>
#include <QtConcurrent>

#include <atomic>
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
    m_w->setListener(m_listener.get());

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
                emit error(friendlyError(r.second));
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
        out << TransactionHistoryModel::toMap(all[i]);
    return out;
}

// --- daemon / sync ---------------------------------------------------------------------------

void Wallet::connectToDaemon(const QString &address, const QString &user, const QString &password, bool trusted)
{
    m_daemonAddress = address;
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
    };
    run<B>(QString(), [](IWallet &w) { return B{w.accounts()}; },
           [this](B b) {
               endReload(Reload::Balances);
               applyAccounts(b.accounts);
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

void Wallet::emitCreated(std::unique_ptr<IPendingTx> tx, const QString &kind, const QStringList &dests, bool flash,
                         const QString &note)
{
    if (!tx)
        return;  // the call threw; run() already reported it
    if (!tx->ok()) {
        emit error(tx->errorString());
        return;
    }
    auto *p = new PendingTransaction(std::move(tx), kind, dests, flash, note, this);
    QQmlEngine::setObjectOwnership(p, QQmlEngine::CppOwnership);
    emit transactionCreated(p);
}

void Wallet::createTransaction(const QVariantList &destinations, bool flash)
{
    QStringList addrs;
    QList<quint64> amounts;
    for (const auto &v : destinations) {
        const auto m = v.toMap();
        const QString a = m.value("address").toString().trimmed();
        const auto amt = m_engine->amountFromString(m.value("amount").toString());
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
    run<std::unique_ptr<IPendingTx>>(tr("Creating transaction…"),
                                     [addrs, amounts, prio, acct](IWallet &w) { return w.createTransaction(addrs, amounts, prio, acct); },
                                     [this, addrs, flash](std::unique_ptr<IPendingTx> tx) {
                                         emitCreated(std::move(tx), QStringLiteral("transfer"), addrs, flash, {});
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
    QPointer<PendingTransaction> guard(tx);
    run<std::pair<bool, QString>>(tr("Broadcasting transaction…"),
                                  [raw, flash](IWallet &w) {
                                      const bool ok = raw->commit(flash);
                                      if (ok)
                                          w.store();
                                      return std::make_pair(ok, ok ? QString() : raw->errorString());
                                  },
                                  [this, guard](std::pair<bool, QString> r) {
                                      const QStringList ids = guard ? guard->raw()->txids() : QStringList{};
                                      emit transactionCommitted(r.first, r.first ? tr("Transaction sent") : r.second, ids);
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
