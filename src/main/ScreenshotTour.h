// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
//
// BELDEX_GUI_SHOTS=<dir> renders every screen to PNG and exits. Demo builds use a simulated wallet;
// real builds create a fresh mainnet wallet and connect to the configured node:
//   QT_QPA_PLATFORM=offscreen BELDEX_GUI_SHOTS=shots ./beldex-wallet-gui
// Used for docs, design review and as a smoke test (any QML error fails the tour).
#pragma once

#include "daemon/DaemonManager.h"
#include "libwalletqt/WalletManager.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QTimer>

#include <chrono>
#include <thread>

#include <functional>
#include <memory>
#include <vector>

class ScreenshotTour : public QObject {
public:
    ScreenshotTour(QQmlApplicationEngine &engine, WalletManager &mgr, QString outDir)
        : m_engine(engine), m_mgr(mgr), m_out(std::move(outDir))
    {
        QDir().mkpath(m_out);
        connect(&m_mgr, &WalletManager::walletError, this, [](const QString &e) { qWarning("tour: walletError: %s", qPrintable(e)); });
        connect(&m_mgr, &WalletManager::walletOpened, this, [] { qInfo("tour: wallet opened"); });
    }

    // Logs every time the UI thread was blocked for more than 300 ms (what a user sees as a frozen window).
    void startStallDetector()
    {
        auto *t = new QTimer(this);
        t->setInterval(50);
        m_stallClock.start();
        connect(t, &QTimer::timeout, this, [this] {
            const qint64 gap = m_stallClock.restart();
            if (gap > 300) {
                ++m_stalls;
                m_maxStall = std::max(m_maxStall, gap);
                qInfo("tour: UI STALL %lld ms (at +%lld s)", gap, m_runClock.elapsed() / 1000);
            }
        });
        m_runClock.start();
        t->start();
    }

    void start()
    {
        startStallDetector();
        if (!m_mgr.isDemo()) {
            startReal();
            return;
        }
        const QString walletDir = QDir(m_out).filePath(QStringLiteral("wallets"));
        QDir(walletDir).removeRecursively();
        const QString seed = QStringLiteral(
            "orbit cedar lantern meadow quartz falcon ember tundra violet anchor pixel harbor nectar "
            "summit willow cobalt radar saffron timber zephyr bamboo glacier onyx velvet orbit");

        step(1500, [this] { shot("01-wizard"); });
        step(200, [this, walletDir, seed] {
            setProp("page", "overview");
            m_mgr.restoreFromSeed(walletDir, QStringLiteral("demo"), QStringLiteral("demo"), seed, 0,
                                  m_mgr.estimateHeightForDate(0, QDate::currentDate().addDays(-90).toString(Qt::ISODate)), {});
        });
        step(3500, [this] { shot("02-overview-syncing"); });
        step(12000, [this] { shot("03-overview"); });
        const char *pages[] = {"send", "receive", "history", "staking", "bns", "contacts", "settings"};
        int n = 4;
        for (const char *p : pages) {
            const QString page = QString::fromLatin1(p);
            const QString name = QStringLiteral("%1-%2").arg(n++, 2, 10, QLatin1Char('0')).arg(page);
            step(300, [this, page] { setProp("page", page); });
            step(900, [this, name] { shot(name); });
        }
        step(100, [this] {
            if (auto *w = m_mgr.wallet())
                w->createTransaction({QVariantMap{{"address", w->subaddresses().value(0).toMap().value("address")},
                                                  {"amount", QStringLiteral("125.5")}}},
                                     false);
            setProp("page", "send");
        });
        step(1500, [this] { shot("11-confirm-send"); });
        step(100, [this] { m_mgr.closeWallet(); });
        step(800, [this] { shot("12-closed"); });
        step(100, [this, walletDir] {
            setProp("wizardHold", true);
            m_mgr.createWallet(walletDir, QStringLiteral("fresh"), QStringLiteral("demo"), 0);
        });
        step(1500, [this] { shot("13-new-wallet-seed"); });
        step(200, [] { QGuiApplication::quit(); });
        next();
    }

    // Real Beldex core: create a brand-new mainnet wallet through WalletManager (in a folder that does not
    // exist yet), let it connect to the configured node, and capture the screens. Exits 1 if a check fails.
    // BELDEX_GUI_TOUR=restore: create a wallet, then restore it from its seed at height 0 and log what
    // the GUI reports while it syncs. Diagnoses "shows Synced but balance is wrong" reports.
    void startRestoreSync()
    {
        const QString walletDir = QDir(m_out).filePath(QStringLiteral("wallets"));
        QDir(walletDir).removeRecursively();
        step(500, [this, walletDir] { setProp("wizardHold", true); m_mgr.createWallet(walletDir, QStringLiteral("orig"), QStringLiteral("x"), 0); });
        step(4000, [this, walletDir] {
            m_seed = m_mgr.newWalletSeed();
            m_mgr.closeWallet();
            setProp("wizardHold", false);
            m_mgr.restoreFromSeed(walletDir, QStringLiteral("restored"), QStringLiteral("x"), m_seed, 0,
                                  qEnvironmentVariableIntValue("BELDEX_GUI_TOUR_RESTORE_HEIGHT"), {});
        });
        // Poll every 3 s until the GUI reports synced (or BELDEX_GUI_TOUR_MINUTES, default 2, runs out).
        const int maxTicks = qEnvironmentVariableIntValue("BELDEX_GUI_TOUR_MINUTES") > 0
                                 ? qEnvironmentVariableIntValue("BELDEX_GUI_TOUR_MINUTES") * 20 : 40;
        step(3000, [this, maxTicks] { pollRestore(0, maxTicks); });
        next();
    }

    void pollRestore(int i, int maxTicks)
    {
        auto *w = m_mgr.wallet();
        if (w && (i < 12 || i % 10 == 0 || w->synchronized()))
            qInfo("tour: t=%4ds conn=%d synced=%d progress=%.3f wallet=%.0f daemon=%.0f pending-jobs=%lld", (i + 1) * 3,
                  w->connectionStatus(), w->synchronized(), w->syncProgress(), w->blockHeight(), w->daemonHeight(),
                  qlonglong(w->findChildren<QFutureWatcherBase *>(Qt::FindDirectChildrenOnly).size()));
        if (i == 6)
            shot("restore-sync-20s");
        if ((w && w->synchronized()) || i + 1 >= maxTicks) {
            check(w && w->synchronized(), "restored wallet reaches Synced");
            check(w && w->blockHeight() + 2 >= w->daemonHeight(), "wallet height matches node height");
            shot("restore-synced");
            std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(120)); qWarning("tour: close still running after 120 s"); std::abort(); }).detach();
            QElapsedTimer t;
            t.start();
            m_mgr.closeWallet();
            qInfo("tour: closeWallet took %lld ms", t.elapsed());
            // Second watchdog: app shutdown after the close must finish quickly too.
            std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(30)); qWarning("tour: shutdown still running after 30 s"); std::abort(); }).detach();
            QCoreApplication::exit(m_failures ? 1 : 0);
            return;
        }
        QTimer::singleShot(3000, this, [this, i, maxTicks] { pollRestore(i + 1, maxTicks); });
    }

    // BELDEX_GUI_TOUR=reopen BELDEX_GUI_TOUR_WALLET=<path> BELDEX_GUI_TOUR_PASSWORD=<pw>: open an existing
    // wallet, let it refresh, then time closing it. A watchdog aborts if close takes > 20 s (for gdb).
    void startReopen()
    {
        const QString path = qEnvironmentVariable("BELDEX_GUI_TOUR_WALLET");
        const QString pw = qEnvironmentVariable("BELDEX_GUI_TOUR_PASSWORD");
        step(500, [this, path, pw] { m_mgr.openWallet(path, pw, 0); });
        // BELDEX_GUI_TOUR_MINUTES=n: first wait (up to n minutes) for the wallet to finish syncing.
        if (const int minutes = qEnvironmentVariableIntValue("BELDEX_GUI_TOUR_MINUTES"); minutes > 0)
            step(5000, [this, minutes] {
                waitSynced(0, minutes * 20, [this] {
                    auto *w = m_mgr.wallet();
                    check(w && w->synchronized() && w->blockHeight() + 2 >= w->daemonHeight(), "wallet syncs to the node's height");
                    shot("reopen-synced");
                });
            });
        step(30000, [this] {
            auto *w = m_mgr.wallet();
            qInfo("tour: before close synced=%d wallet=%.0f daemon=%.0f", w && w->synchronized(), w ? w->blockHeight() : 0.0,
                  w ? w->daemonHeight() : 0.0);
            std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(20)); qWarning("tour: close still running after 20 s"); std::abort(); }).detach();
            QElapsedTimer t;
            t.start();
            m_mgr.closeWallet();
            qInfo("tour: closeWallet took %lld ms", t.elapsed());
            QCoreApplication::exit(0);
        });
        next();
    }

    // BELDEX_GUI_TOUR=resume: the "restored wallet closed before its first scan" bug.
    // Restore at height H with the node unreachable (no scan), close, reopen with the node back:
    // the wallet must resume from H, not fast-forward to the tip. Then test rescan-from-height.
    void startResume()
    {
        const QString walletDir = QDir(m_out).filePath(QStringLiteral("wallets"));
        QDir(walletDir).removeRecursively();
        const QString node = appSetting("mainnetNode").toString();
        step(500, [this, walletDir] { setProp("wizardHold", true); m_mgr.createWallet(walletDir, QStringLiteral("orig"), QStringLiteral("x"), 0); });
        step(5000, [this, walletDir] {
            m_seed = m_mgr.newWalletSeed();
            m_tip = m_mgr.wallet() ? quint64(m_mgr.wallet()->daemonHeight()) : 0;
            m_mgr.closeWallet();
            setProp("wizardHold", false);
            setAppSetting("mainnetNodeMode", QStringLiteral("remote"));   // Auto would fall back to a public node
            setAppSetting("mainnetNode", QStringLiteral("127.0.0.1:1"));   // node down: restore cannot scan
            m_h = m_tip - 40000;
            qInfo("tour: tip %llu, restoring at %llu with node down", (unsigned long long)m_tip, (unsigned long long)m_h);
            m_mgr.restoreFromSeed(walletDir, QStringLiteral("restored"), QStringLiteral("x"), m_seed, 0, double(m_h), {});
        });
        step(6000, [this, walletDir, node] {
            check(m_mgr.wallet() && m_mgr.wallet()->blockHeight() <= 1, "restored wallet has not scanned (node down)");
            m_mgr.closeWallet();                                              // like the crash: closed before any scan
            setAppSetting("mainnetNode", node);
            setAppSetting("mainnetNodeMode", QStringLiteral("auto"));
            m_mgr.openWallet(QDir(walletDir).filePath(QStringLiteral("restored/restored")), QStringLiteral("x"), 0);
        });
        step(8000, [this] {
            auto *w = m_mgr.wallet();
            qInfo("tour: after reopen restore=%.0f wallet=%.0f daemon=%.0f synced=%d", w ? w->restoreHeight() : -1.0,
                  w ? w->blockHeight() : -1.0, w ? w->daemonHeight() : -1.0, w && w->synchronized());
            check(w && quint64(w->restoreHeight()) == m_h, "reopened wallet keeps its restore height");
            check(w && (!w->synchronized() || w->blockHeight() < m_tip), "reopened wallet scans instead of jumping to the tip");
        });
        step(3000, [this] { waitSynced(0, 200, [this] {
            check(m_mgr.wallet() && m_mgr.wallet()->synchronized(), "resumed restore reaches Synced");
            m_h2 = m_tip - 20000;
            m_mgr.wallet()->rescanBlockchainFrom(double(m_h2));
        }); });
        step(4000, [this] {
            auto *w = m_mgr.wallet();
            qInfo("tour: after rescan restore=%.0f wallet=%.0f synced=%d", w->restoreHeight(), w->blockHeight(), w->synchronized());
            check(quint64(w->restoreHeight()) == m_h2, "rescan uses the new start height");
            check(!w->synchronized(), "rescan is in progress");
        });
        step(3000, [this] { waitSynced(0, 200, [this] {
            check(m_mgr.wallet() && m_mgr.wallet()->synchronized(), "rescan reaches Synced");
            shot("resume-rescanned");
            m_mgr.closeWallet();
            qInfo("tour: UI stalls >300ms: %d, longest %lld ms", m_stalls, m_maxStall);
            qInfo("tour: %s (%d failure%s)", m_failures ? "FAILED" : "OK", m_failures, m_failures == 1 ? "" : "s");
            QCoreApplication::exit(m_failures ? 1 : 0);
        }); });
        next();
    }

    // Calls `then` once the wallet reports synced (polls every 3 s, up to maxTicks), then continues the tour.
    void waitSynced(int i, int maxTicks, std::function<void()> then)
    {
        m_hold = true;
        auto *w = m_mgr.wallet();
        if ((w && w->synchronized()) || i >= maxTicks) {
            m_hold = false;
            then();
            next();
            return;
        }
        if (w && i % 5 == 0)
            qInfo("tour:   syncing %.1f%% wallet=%.0f daemon=%.0f", w->syncProgress() * 100, w->blockHeight(), w->daemonHeight());
        QTimer::singleShot(3000, this, [this, i, maxTicks, then] { waitSynced(i + 1, maxTicks, then); });
    }

    QVariant appSetting(const char *name)
    {
        auto *app = m_engine.singletonInstance<QObject *>("BeldexGui", "App");
        auto *settings = app ? app->property("settings").value<QObject *>() : nullptr;
        return settings ? settings->property(name) : QVariant();
    }
    void setAppSetting(const char *name, const QVariant &v)
    {
        auto *app = m_engine.singletonInstance<QObject *>("BeldexGui", "App");
        if (auto *settings = app ? app->property("settings").value<QObject *>() : nullptr)
            settings->setProperty(name, v);
    }

    // BELDEX_GUI_TOUR=midscan: wallet actions and closing must not wait for a running scan to finish.
    void startMidScan()
    {
        const QString walletDir = QDir(m_out).filePath(QStringLiteral("wallets"));
        QDir(walletDir).removeRecursively();
        const QString path = QDir(walletDir).filePath(QStringLiteral("restored/restored"));
        step(500, [this, walletDir] { setProp("wizardHold", true); m_mgr.createWallet(walletDir, QStringLiteral("orig"), QStringLiteral("x"), 0); });
        step(5000, [this, walletDir] {
            m_seed = m_mgr.newWalletSeed();
            m_mgr.closeWallet();
            setProp("wizardHold", false);
            m_mgr.restoreFromSeed(walletDir, QStringLiteral("restored"), QStringLiteral("x"), m_seed, 0, 0, {});
        });
        step(45000, [this] {
            auto *w = m_mgr.wallet();
            check(w && !w->synchronized() && w->blockHeight() > 1000, "wallet is mid-scan");
            QElapsedTimer t;
            t.start();
            const QVariantMap sec = w->secrets(QStringLiteral("x"));
            const qint64 ms = t.elapsed();
            qInfo("tour: secrets() during scan took %lld ms", ms);
            check(sec.value("seed").toString() == m_seed && ms < 15000, "seed readable during scan, quickly");
            w->addSubaddress(QStringLiteral("mid-scan"));
        });
        step(8000, [this] {
            auto *w = m_mgr.wallet();
            check(w && w->subaddresses().size() == 2, "subaddress added during scan");
            m_scanHeightBeforeClose = w ? quint64(w->blockHeight()) : 0;
        });
        step(30000, [this] {
            auto *w = m_mgr.wallet();
            check(w && !w->accounts().isEmpty(), "balance snapshot taken during scan");
            m_scanHeightBeforeClose = w ? quint64(w->blockHeight()) : 0;
            qInfo("tour: closing at scan height %llu", (unsigned long long)m_scanHeightBeforeClose);
            QElapsedTimer t;
            t.start();
            m_mgr.closeWallet();
            const qint64 ms = t.elapsed();
            qInfo("tour: closeWallet during scan took %lld ms", ms);
            check(ms < 15000, "closing during a scan returns within 15 s");
        });
        step(500, [this, path] { m_mgr.openWallet(path, QStringLiteral("x"), 0); });
        step(5000, [this] {
            auto *w = m_mgr.wallet();
            qInfo("tour: reopened at height %.0f (closed at %llu)", w ? w->blockHeight() : -1.0,
                  (unsigned long long)m_scanHeightBeforeClose);
            check(w && w->blockHeight() + 50000 > m_scanHeightBeforeClose && w->blockHeight() > 1000,
                  "reopened wallet resumes from saved scan progress");
            QElapsedTimer t;
            t.start();
            m_mgr.closeWallet();
            check(t.elapsed() < 15000, "second close during scan is quick too");
            qInfo("tour: UI stalls >300ms: %d, longest %lld ms", m_stalls, m_maxStall);
            qInfo("tour: %s (%d failure%s)", m_failures ? "FAILED" : "OK", m_failures, m_failures == 1 ? "" : "s");
            QCoreApplication::exit(m_failures ? 1 : 0);
        });
        next();
    }

    // BELDEX_GUI_TOUR=testnet-local | mainnet-local: the local node path. Creates a wallet on that network
    // with node mode Auto (testnet: no public node, so it must start beldexd) or Local (mainnet), using a
    // throwaway blockchain folder, and checks beldexd starts, answers RPC, the wallet connects, and that
    // closing the app stops the node.
    void startLocalNode(int net)
    {
        const QString walletDir = QDir(m_out).filePath(QStringLiteral("wallets"));
        QDir(walletDir).removeRecursively();
        const QString chainDir = QDir(m_out).filePath(QStringLiteral("chain"));
        setAppSetting("daemonDataDir", chainDir);
        setAppSetting(net == 1 ? "testnetNodeMode" : net == 2 ? "devnetNodeMode" : "mainnetNodeMode",
                      net == 0 ? QStringLiteral("local") : QStringLiteral("auto"));   // test nets: Auto must fall back
        auto *dm = DaemonManager::instance();
        step(500, [this, walletDir, net] { setProp("wizardHold", true); m_mgr.createWallet(walletDir, QStringLiteral("local"), QStringLiteral("x"), net); });
        step(3000, [this] { m_mgr.clearNewWalletSeed(); setProp("wizardHold", false); setProp("page", "overview"); });
        step(net == 0 ? 60000 : 25000, [this, dm, net] {
            auto *w = m_mgr.wallet();
            qInfo("tour: daemon running=%d owns=%d net=%d height=%.0f target=%.0f peers=%d status=\"%s\"", dm->running(),
                  dm->ownsProcess(), dm->netType(), dm->height(), dm->targetHeight(), dm->peers(), qPrintable(dm->statusText()));
            qInfo("tour: wallet net=%d connected=%d daemonAddress=%s", w ? w->netType() : -1, w && w->connected(),
                  w ? qPrintable(w->daemonAddress()) : "-");
            if (net == 2) {
                // Known Beldex core bug: devnet's genesis block is v1 but its fork table starts at v7 at height 0,
                // so beldexd --devnet can't start. The app must say why instead of hanging or pretending.
                qInfo("tour: devnet node error: %s", qPrintable(dm->lastError()));
                check(!dm->running(), "devnet node is not reported as running");
                check(dm->lastError().contains(QLatin1String("genesis")), "devnet failure reported with core's reason");
            } else {
                check(dm->ownsProcess() && dm->running(), "app started beldexd and its RPC answers");
                check(dm->netType() == net, "local node runs on the wallet's network");
                check(w && w->daemonAddress() == dm->rpcAddress(), "wallet is pointed at the local node");
                check(w && w->connected(), "wallet connected to the local node");
            }
            if (net == 1 && dm->peers() == 0)
                check(!dm->synchronized(), "a peerless testnet node is not reported as synced");
            if (net == 0) {
                check(dm->peers() > 0, "local mainnet node found peers");
                check(dm->height() > 1, "local mainnet node is downloading blocks");
            }
            shot(net == 1 ? "testnet-local-overview" : net == 2 ? "devnet-local-overview" : "mainnet-local-overview");
            setProp("page", "settings");
        });
        step(1500, [this, net] { shot(net == 1 ? "testnet-local-settings" : net == 2 ? "devnet-local-settings" : "mainnet-local-settings"); });
        if (net != 0) {
            // Opening this wallet with the selector on mainnet must still open it on its own network.
            step(200, [this, walletDir] {
                m_mgr.closeWallet();
                m_mgr.openWallet(QDir(walletDir).filePath(QStringLiteral("local/local")), QStringLiteral("x"), 0);
            });
            step(6000, [this, net] {
                auto *w = m_mgr.wallet();
                check(w && w->netType() == net, "wallet opened on its own network although mainnet was selected");
            });
        }
        step(1000, [this, dm] {
            // Quit the way a user does: close the window. Main.qml saves the wallet and stops the node in
            // the background behind a shutdown screen, then quits; the UI must never freeze meanwhile.
            m_quitClock.start();
            connect(qApp, &QCoreApplication::aboutToQuit, this, [this, dm] {
                qInfo("tour: app quit %lld ms after the window was closed", m_quitClock.elapsed());
                check(!dm->ownsProcess(), "local node stopped before the app quit");
                check(!m_mgr.walletOpen() && !m_mgr.closing(), "wallet saved and closed before the app quit");
                check(m_stalls == 0, "UI never froze (no stall > 300 ms)");
                qInfo("tour: UI stalls >300ms: %d, longest %lld ms", m_stalls, m_maxStall);
                qInfo("tour: %s (%d failure%s)", m_failures ? "FAILED" : "OK", m_failures, m_failures == 1 ? "" : "s");
            });
            if (auto *win = qobject_cast<QQuickWindow *>(m_engine.rootObjects().value(0)))
                win->close();
        });
        step(1200, [this] { shot("quitting-overlay"); });
        next();
    }

    // BELDEX_GUI_TOUR=switch: one seed on two networks. Creates a mainnet wallet, switches it to testnet (a new
    // copy that scans from BELDEX_GUI_TOUR_RESTORE_HEIGHT, or the node's height when unset), waits for it to sync
    // through the testnet node, then switches back and forth and checks each side reopens with its own address.
    void startSwitch()
    {
        const QString walletDir = QDir(m_out).filePath(QStringLiteral("wallets"));
        QDir(walletDir).removeRecursively();
        const int minutes = qEnvironmentVariableIntValue("BELDEX_GUI_TOUR_MINUTES") > 0 ? qEnvironmentVariableIntValue("BELDEX_GUI_TOUR_MINUTES") : 5;
        step(500, [this, walletDir] { m_mgr.createWallet(walletDir, QStringLiteral("acct"), QStringLiteral("x"), 0); });
        step(5000, [this] {
            auto *w = m_mgr.wallet();
            check(w && w->netType() == 0, "mainnet wallet open");
            m_mainAddr = w ? w->address() : QString();
            const QVariantMap sib = m_mgr.networkSibling(1);
            check(sib.value("name").toString() == QLatin1String("acct-testnet") && !sib.value("exists").toBool(),
                  "testnet copy will be acct-testnet (new)");
            check(!m_mgr.switchNetwork(1, QStringLiteral("wrong"), 0).isEmpty() && m_mgr.wallet() == w,
                  "wrong password is refused and the wallet stays open");
            if (auto *app = m_engine.singletonInstance<QObject *>("BeldexGui", "App"))
                QMetaObject::invokeMethod(app, "switchNetwork");
        });
        step(2500, [this] { shot("switch-dialog-new"); });
        step(200, [this] {
            const double h = qEnvironmentVariableIsSet("BELDEX_GUI_TOUR_RESTORE_HEIGHT")
                                 ? qEnvironmentVariableIntValue("BELDEX_GUI_TOUR_RESTORE_HEIGHT") : 0;
            const QString err = m_mgr.switchNetwork(1, QStringLiteral("x"), h);
            check(err.isEmpty(), "switch to testnet started");
            if (!err.isEmpty())
                qInfo("tour:   %s", qPrintable(err));
        });
        step(8000, [this, minutes] {
            auto *w = m_mgr.wallet();
            check(w && w->netType() == 1, "testnet copy open");
            check(w && m_mgr.addressValid(w->address(), 1) && w->address() != m_mainAddr, "testnet copy has a testnet address");
            m_testAddr = w ? w->address() : QString();
            shot("switch-testnet-syncing");
            waitSynced(0, minutes * 20, [this] {
                auto *w = m_mgr.wallet();
                check(w && w->synchronized(), "testnet copy syncs through the testnet node");
                check(w && w->blockHeight() + 2 >= w->daemonHeight() && w->daemonHeight() > 4000000, "testnet copy reaches the node's height");
                if (w)
                    qInfo("tour:   testnet wallet=%.0f daemon=%.0f node=%s", w->blockHeight(), w->daemonHeight(), qPrintable(w->daemonAddress()));
                shot("switch-testnet-synced");
                const QVariantMap sib = m_mgr.networkSibling(0);
                check(sib.value("name").toString() == QLatin1String("acct") && sib.value("exists").toBool(), "mainnet side found as acct");
                check(m_mgr.switchNetwork(0, QStringLiteral("x"), 0).isEmpty(), "switch back to mainnet started");
            });
        });
        step(8000, [this] {
            auto *w = m_mgr.wallet();
            check(w && w->netType() == 0 && w->address() == m_mainAddr, "back on the original mainnet wallet");
            const QVariantMap sib = m_mgr.networkSibling(1);
            check(sib.value("name").toString() == QLatin1String("acct-testnet") && sib.value("exists").toBool(), "testnet copy is found again");
            check(m_mgr.switchNetwork(1, QStringLiteral("x"), 0).isEmpty(), "switch to testnet again started");
        });
        step(8000, [this] {
            auto *w = m_mgr.wallet();
            check(w && w->netType() == 1 && w->address() == m_testAddr, "the same testnet copy reopens (no new wallet)");
            check(!QFileInfo::exists(QDir(m_out).filePath(QStringLiteral("wallets/acct/acct-testnet-2.keys"))), "no duplicate copies");
            shot("switch-testnet-reopened");
            qInfo("tour: %s (%d failure%s)", m_failures ? "FAILED" : "OK", m_failures, m_failures == 1 ? "" : "s");
            QCoreApplication::exit(m_failures ? 1 : 0);
        });
        next();
    }

    void startReal()
    {
        if (qEnvironmentVariable("BELDEX_GUI_TOUR") == QLatin1String("switch")) {
            startSwitch();
            return;
        }
        if (qEnvironmentVariable("BELDEX_GUI_TOUR") == QLatin1String("testnet-local")) {
            startLocalNode(1);
            return;
        }
        if (qEnvironmentVariable("BELDEX_GUI_TOUR") == QLatin1String("devnet-local")) {
            startLocalNode(2);
            return;
        }
        if (qEnvironmentVariable("BELDEX_GUI_TOUR") == QLatin1String("mainnet-local")) {
            startLocalNode(0);
            return;
        }
        if (qEnvironmentVariable("BELDEX_GUI_TOUR") == QLatin1String("midscan")) {
            startMidScan();
            return;
        }
        if (qEnvironmentVariable("BELDEX_GUI_TOUR") == QLatin1String("resume")) {
            startResume();
            return;
        }
        if (qEnvironmentVariable("BELDEX_GUI_TOUR") == QLatin1String("reopen")) {
            startReopen();
            return;
        }
        if (qEnvironmentVariable("BELDEX_GUI_TOUR") == QLatin1String("restore")) {
            startRestoreSync();
            return;
        }
        const QString walletDir = QDir(m_out).filePath(QStringLiteral("wallets"));
        QDir(walletDir).removeRecursively();
        step(1500, [this] { shot("01-wizard"); });
        step(100, [this, walletDir] {
            setProp("wizardHold", true);
            m_mgr.createWallet(walletDir, QStringLiteral("smoke"), QStringLiteral("smoke"), 0);
        });
        step(6000, [this] {
            shot("02-new-wallet-seed");
            check(m_mgr.walletOpen(), "wallet created through WalletManager");
            check(m_mgr.newWalletSeed().split(' ', Qt::SkipEmptyParts).size() == 25, "seed shown has 25 words");
            m_mgr.clearNewWalletSeed();
            setProp("wizardHold", false);
            setProp("page", "overview");
        });
        step(25000, [this] {
            shot("03-overview");
            auto *w = m_mgr.wallet();
            check(w && w->primaryAddress().startsWith(QStringLiteral("bx")), "primary address is bx…");
            check(w && w->connected(), "connected to node");
            check(w && w->daemonHeight() > 5000000, "network height read");
            check(w && w->synchronized(), "new wallet synchronized");
            if (w)
                qInfo("tour: node %s height %.0f wallet height %.0f hf v%d", qPrintable(w->daemonAddress()),
                      w->daemonHeight(), w->blockHeight(), w->hardFork());
        });
        const char *pages[] = {"send", "receive", "history", "staking", "bns", "contacts", "settings"};
        int n = 4;
        for (const char *p : pages) {
            const QString page = QString::fromLatin1(p);
            const QString name = QStringLiteral("%1-%2").arg(n++, 2, 10, QLatin1Char('0')).arg(page);
            step(300, [this, page] { setProp("page", page); });
            step(1200, [this, name] { shot(name); });
        }
        step(100, [this] {
            if (auto *w = m_mgr.wallet())
                w->addSubaddress(QStringLiteral("tour"));
            setProp("page", "receive");
        });
        step(4000, [this] {   // adding a subaddress also saves the wallet file
            shot("11-receive-subaddress");
            check(m_mgr.wallet() && m_mgr.wallet()->subaddresses().size() == 2, "subaddress added");
            // Regression: with no reachable node, core throws no_connection_to_daemon out of
            // estimateTransactionFee & co. That used to abort the app (QUnhandledException).
            if (auto *w = m_mgr.wallet()) {
                connect(w, &Wallet::error, this, [this](const QString &e) { ++m_walletErrors; qInfo("tour: wallet error: %s", qPrintable(e)); });
                w->connectToDaemon(QStringLiteral("127.0.0.1:1"));
            }
        });
        step(4000, [this] {
            if (auto *w = m_mgr.wallet()) {
                w->estimateFee(1, false);
                w->stake(QString(64, QLatin1Char('a')), QStringLiteral("1"));
                w->refreshAll();
            }
        });
        step(4000, [this] {
            shot("12-node-down");
            check(m_mgr.walletOpen(), "app survives core exceptions with the node down");
            check(m_walletErrors > 0, "node-down errors reported to the UI");
            m_mgr.closeWallet();
        });
        step(1000, [this] {
            check(!m_mgr.walletOpen(), "wallet closed");
            qInfo("tour: UI stalls >300ms: %d, longest %lld ms", m_stalls, m_maxStall);
            qInfo("tour: %s (%d failure%s)", m_failures ? "FAILED" : "OK", m_failures, m_failures == 1 ? "" : "s");
            QCoreApplication::exit(m_failures ? 1 : 0);
        });
        next();
    }

private:
    void check(bool ok, const char *what)
    {
        qInfo("tour: %s  %s", ok ? "PASS" : "FAIL", what);
        if (!ok)
            ++m_failures;
    }
    void step(int delayMs, std::function<void()> fn) { m_steps.push_back({delayMs, std::move(fn)}); }
    void next()
    {
        if (m_i >= m_steps.size())
            return;
        auto &s = m_steps[m_i++];
        QTimer::singleShot(s.first, this, [this, &s] {
            s.second();
            if (!m_hold)   // an async step (waitSynced) resumes the sequence itself
                next();
        });
    }
    void setProp(const char *name, const QVariant &v)
    {
        if (auto *app = m_engine.singletonInstance<QObject *>("BeldexGui", "App"))
            app->setProperty(name, v);
    }
    void shot(const QString &name)
    {
        if (m_engine.rootObjects().isEmpty())
            return;
        if (auto *win = qobject_cast<QQuickWindow *>(m_engine.rootObjects().first())) {
            const QString f = QDir(m_out).filePath(name + QStringLiteral(".png"));
            win->grabWindow().save(f);
            qInfo("screenshot: %s", qPrintable(f));
        }
    }

    QQmlApplicationEngine &m_engine;
    WalletManager &m_mgr;
    QString m_out;
    std::vector<std::pair<int, std::function<void()>>> m_steps;
    size_t m_i = 0;
    int m_failures = 0;
    int m_walletErrors = 0;
    QElapsedTimer m_stallClock, m_runClock, m_quitClock;
    int m_stalls = 0;
    qint64 m_maxStall = 0;
    QString m_seed;
    quint64 m_tip = 0, m_h = 0, m_h2 = 0;
    bool m_hold = false;
    QString m_mainAddr, m_testAddr;
    quint64 m_scanHeightBeforeClose = 0;
};
