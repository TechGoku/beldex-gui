// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#include "WalletManager.h"

#include "Wallet.h"

#include <QClipboard>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QtConcurrent>

#ifndef BELDEX_GUI_VERSION
#define BELDEX_GUI_VERSION "0.0.0"
#endif

using namespace bdx;

static WalletManager *s_instance = nullptr;

// Record a restore in the wallet file itself, so a wallet that is closed (or crashes) before its first
// scan resumes the restore on reopen instead of being fast-forwarded as "new" (see Wallet::connectToDaemon).
static void markRestored(IWallet *w, quint64 height)
{
    if (!w)
        return;
    w->setAttribute(QString::fromLatin1(Wallet::kRestoreHeightAttr), QString::number(height));
    w->setAttribute(QString::fromLatin1(Wallet::kRestorePendingAttr), QStringLiteral("1"));
    w->store();
}

WalletManager::WalletManager(QObject *parent) : QObject(parent), m_engine(createEngine())
{
    m_pool.setMaxThreadCount(1);
    s_instance = this;
}

WalletManager::~WalletManager()
{
    closeWallet();
    m_pool.waitForDone();
    if (s_instance == this)
        s_instance = nullptr;
}

WalletManager *WalletManager::create(QQmlEngine *, QJSEngine *)
{
    auto *m = s_instance ? s_instance : new WalletManager(nullptr);
    QQmlEngine::setObjectOwnership(m, QQmlEngine::CppOwnership);
    return m;
}

WalletManager *WalletManager::instance() { return s_instance; }

QString WalletManager::defaultWalletDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(base).filePath(isDemo() ? QStringLiteral("Beldex/demo-wallets") : QStringLiteral("Beldex/wallets"));
}

QString WalletManager::version() const { return QStringLiteral(BELDEX_GUI_VERSION); }

void WalletManager::setBusy(bool b, const QString &text)
{
    m_busy = b;
    m_busyText = text;
    emit busyChanged();
}

void WalletManager::runOpen(const QString &busyText, Factory f, bool captureSeed)
{
    if (m_busy)
        return;
    closeWallet();
    setBusy(true, busyText);
    IEngine *engine = m_engine.get();
    struct Result {
        std::unique_ptr<IWallet> wallet;
        QString error, seed;
    };
    QFuture<Result> fut = QtConcurrent::run(&m_pool, [engine, f = std::move(f), captureSeed]() {
        Result r;
        try {
            r.wallet = f(*engine);
        } catch (const std::exception &e) {  // never let a core exception abort the app
            r.error = QString::fromLocal8Bit(e.what());
            return r;
        }
        if (!r.wallet)
            r.error = engine->errorString().isEmpty() ? QObject::tr("Unknown error") : engine->errorString();
        else if (captureSeed)
            r.seed = r.wallet->seed();
        return r;
    });
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, fut]() mutable {
        watcher->deleteLater();
        Result r = fut.takeResult();
        setBusy(false);
        if (!r.wallet) {
            emit walletError(r.error);
            return;
        }
        m_newSeed = r.seed;
        emit newWalletSeedChanged();
        m_wallet = new Wallet(m_engine.get(), std::move(r.wallet), this);
        QQmlEngine::setObjectOwnership(m_wallet, QQmlEngine::CppOwnership);
        emit walletChanged();
        emit walletOpened();
    });
    watcher->setFuture(fut);
}

void WalletManager::createWallet(const QString &dir, const QString &name, const QString &password, int net)
{
    const QString path = walletPath(dir, name);
    QDir().mkpath(QFileInfo(path).absolutePath());  // wallets live in <dir>/<name>/<name>
    runOpen(tr("Creating wallet…"),
            [path, password, net](IEngine &e) { return e.createWallet(path, password, QStringLiteral("English"), NetType(net)); },
            true);
}

void WalletManager::openWallet(const QString &path, const QString &password, int net)
{
    QString p = path;
    if (p.endsWith(QStringLiteral(".keys")))
        p.chop(5);
    runOpen(tr("Opening wallet…"),
            [p, password, net](IEngine &e) {
                auto w = e.openWallet(p, password, NetType(net));
                // A wallet knows its own network; core refuses others with "Testnet wallet cannot be opened as
                // mainnet wallet". Retry on the other networks so the network selector never gets in the way.
                if (!w && e.errorString().contains(QLatin1String("cannot be opened as"))) {
                    const QString firstError = e.errorString();
                    for (int other : {0, 1, 2}) {
                        if (other == net)
                            continue;
                        if ((w = e.openWallet(p, password, NetType(other))))
                            break;
                    }
                    if (!w)
                        qWarning("wallet: %s", qPrintable(firstError));
                }
                return w;
            },
            false);
}

void WalletManager::restoreFromSeed(const QString &dir, const QString &name, const QString &password, const QString &seed,
                                    int net, double height, const QString &offset)
{
    const QString path = walletPath(dir, name);
    QDir().mkpath(QFileInfo(path).absolutePath());  // wallets live in <dir>/<name>/<name>
    runOpen(tr("Restoring wallet…"),
            [=](IEngine &e) {
                auto w = e.restoreFromSeed(path, password, seed, NetType(net), quint64(std::max(0.0, height)), offset);
                markRestored(w.get(), quint64(std::max(0.0, height)));
                return w;
            },
            false);
}

void WalletManager::restoreFromKeys(const QString &dir, const QString &name, const QString &password, int net,
                                    double height, const QString &address, const QString &viewKey, const QString &spendKey)
{
    const QString path = walletPath(dir, name);
    QDir().mkpath(QFileInfo(path).absolutePath());  // wallets live in <dir>/<name>/<name>
    runOpen(tr("Restoring wallet…"),
            [=](IEngine &e) {
                auto w = e.restoreFromKeys(path, password, NetType(net), quint64(std::max(0.0, height)), address.trimmed(),
                                           viewKey.trimmed(), spendKey.trimmed());
                markRestored(w.get(), quint64(std::max(0.0, height)));
                return w;
            },
            false);
}

void WalletManager::closeWallet()
{
    if (!m_wallet)
        return;
    Wallet *w = m_wallet;
    m_wallet = nullptr;
    emit walletChanged();
    m_newSeed.clear();
    emit newWalletSeedChanged();

    // Saving the wallet and shutting down core take a second or more (longer for big wallets), so do it
    // off the GUI thread. Opening another wallet queues behind this on the same single-thread pool.
    w->beginRelease();
    ++m_closing;
    emit closingChanged();
    IEngine *engine = m_engine.get();
    QFuture<void> fut = QtConcurrent::run(&m_pool, [w, engine] {
        auto raw = w->finishRelease();
        if (!raw)
            return;
        try {
            engine->closeWallet(std::move(raw), true);
        } catch (const std::exception &e) {
            qWarning("wallet: close failed: %s", e.what());
        }
    });
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, w] {
        watcher->deleteLater();
        w->deleteLater();
        --m_closing;
        emit closingChanged();
        emit walletClosed();
    });
    watcher->setFuture(fut);
}

void WalletManager::waitForClose()
{
    m_pool.waitForDone();
}

void WalletManager::clearNewWalletSeed()
{
    m_newSeed.clear();
    emit newWalletSeedChanged();
}

// --- helpers ---------------------------------------------------------------------------------

QString WalletManager::walletPath(const QString &dir, const QString &name) const
{
    return QDir(dir).filePath(QStringLiteral("%1/%1").arg(name.trimmed()));
}

bool WalletManager::walletExists(const QString &path) const { return m_engine->walletExists(path); }

QVariantList WalletManager::findWallets(const QString &dir) const
{
    QVariantList out;
    QDir root(dir);
    if (!root.exists())
        return out;
    const auto entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo &d : entries) {
        const QString path = QDir(d.absoluteFilePath()).filePath(d.fileName());
        if (QFileInfo::exists(path + QStringLiteral(".keys")))
            out << QVariantMap{{"name", d.fileName()}, {"path", path},
                               {"modified", QFileInfo(path + QStringLiteral(".keys")).lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"))}};
    }
    return out;
}

QString WalletManager::urlToLocalFile(const QUrl &url) const { return url.isLocalFile() ? url.toLocalFile() : url.toString(); }

QString WalletManager::localFileToUrl(const QString &path) const { return QUrl::fromLocalFile(path).toString(); }

bool WalletManager::addressValid(const QString &address, int net) const
{
    return m_engine->addressValid(address.trimmed(), NetType(net));
}

bool WalletManager::masterNodeKeyValid(const QString &key) const { return m_engine->masterNodeKeyValid(key.trimmed()); }

bool WalletManager::amountValid(const QString &amount) const
{
    const auto a = m_engine->amountFromString(amount);
    return a && *a > 0;
}

double WalletManager::estimateHeightForDate(int net, const QString &isoDate) const
{
    return double(m_engine->estimateHeightForDate(NetType(net), QDate::fromString(isoDate, Qt::ISODate)));
}

QVariantMap WalletManager::parseUri(const QString &uri) const
{
    QString u = uri.trimmed();
    if (!u.startsWith(QStringLiteral("beldex:"), Qt::CaseInsensitive))
        return {{"address", u}};
    u = u.mid(7);
    const int q = u.indexOf(QLatin1Char('?'));
    QVariantMap out{{"address", q < 0 ? u : u.left(q)}};
    if (q >= 0) {
        const QUrlQuery query(u.mid(q + 1));
        out["amount"] = query.queryItemValue(QStringLiteral("tx_amount"));
        out["description"] = query.queryItemValue(QStringLiteral("tx_description"), QUrl::FullyDecoded);
    }
    return out;
}

void WalletManager::copyToClipboard(const QString &text) const
{
    if (auto *cb = QGuiApplication::clipboard())
        cb->setText(text);
}

bool WalletManager::saveTextFile(const QUrl &url, const QString &text) const
{
    QFile f(urlToLocalFile(url));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    return f.write(text.toUtf8()) >= 0;
}

QString WalletManager::prettyPath(const QString &path) const
{
    const QString home = QDir::homePath();
    const QString p = QDir::toNativeSeparators(path);
    const QString h = QDir::toNativeSeparators(home);
    if (!h.isEmpty() && (p == h || p.startsWith(h + QDir::separator())))
        return QStringLiteral("~") + p.mid(h.size());
    return p;
}

QString WalletManager::shortAddress(const QString &a, int keep) const
{
    if (a.size() <= keep * 2 + 1)
        return a;
    return a.left(keep) + QStringLiteral("…") + a.right(keep);
}
