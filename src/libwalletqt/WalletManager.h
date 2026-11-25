// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "engine/WalletEngine.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QThreadPool>

#include <memory>

#include "Wallet.h"

// Singleton entry point for QML: opens/creates/restores/closes wallets and offers
// wallet-independent helpers (amount parsing, address validation, file lookups).
class WalletManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Wallet *wallet READ wallet NOTIFY walletChanged)
    Q_PROPERTY(bool walletOpen READ walletOpen NOTIFY walletChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool closing READ closing NOTIFY closingChanged)   // a wallet is being saved and closed
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)
    Q_PROPERTY(bool isDemo READ isDemo CONSTANT)
    Q_PROPERTY(QString engineName READ engineName CONSTANT)
    Q_PROPERTY(QString logPath READ logPath CONSTANT)
    Q_PROPERTY(QString defaultWalletDir READ defaultWalletDir CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    // Mainnet, testnet and devnet are all offered. BELDEX_GUI_MAINNET_ONLY=1 hides testnet/devnet.
    Q_PROPERTY(bool showTestNetworks READ showTestNetworks CONSTANT)
    // Seed of a wallet created in this session, shown once by the wizard, then cleared.
    Q_PROPERTY(QString newWalletSeed READ newWalletSeed NOTIFY newWalletSeedChanged)

public:
    // Not default-constructible on purpose: QML must use create(), i.e. the instance main() owns.
    explicit WalletManager(QObject *parent);
    ~WalletManager() override;

    static WalletManager *create(QQmlEngine *, QJSEngine *);
    static WalletManager *instance();

    Wallet *wallet() const { return m_wallet; }
    bool walletOpen() const { return m_wallet != nullptr; }
    bool busy() const { return m_busy; }
    bool closing() const { return m_closing > 0; }
    void waitForClose();   // blocks until a background close has finished (app exit)
    QString busyText() const { return m_busyText; }
    bool isDemo() const { return m_engine->isDemo(); }
    QString engineName() const { return m_engine->name(); }
    QString logPath() const { return m_engine->logPath(); }
    QString defaultWalletDir() const;
    QString version() const;
    bool showTestNetworks() const { return !qEnvironmentVariableIsSet("BELDEX_GUI_MAINNET_ONLY"); }
    QString newWalletSeed() const { return m_newSeed; }

    // All async; emit walletOpened() or walletError(message).
    Q_INVOKABLE void createWallet(const QString &dir, const QString &name, const QString &password, int netType);
    Q_INVOKABLE void openWallet(const QString &path, const QString &password, int netType);
    Q_INVOKABLE void restoreFromSeed(const QString &dir, const QString &name, const QString &password, const QString &seed,
                                     int netType, double restoreHeight, const QString &seedOffset);
    Q_INVOKABLE void restoreFromKeys(const QString &dir, const QString &name, const QString &password, int netType,
                                     double restoreHeight, const QString &address, const QString &viewKey,
                                     const QString &spendKey);
    Q_INVOKABLE void closeWallet();

    // The same seed on another network: a wallet file is tied to one network, so the open wallet's copy for
    // another network lives next to it (<name>-testnet, …; the copy of a testnet-born wallet on mainnet is
    // <name>-mainnet). networkSibling() returns {name, path, exists} for the open wallet on `netType`.
    Q_INVOKABLE QVariantMap networkSibling(int netType) const;
    // Checks the password (the open wallet's, or the existing copy's), then closes the open wallet and opens its
    // copy on `netType`, creating it from the seed if needed; a new copy scans from `restoreHeight`. Returns an
    // error message without closing anything, or "" once the switch has started (then walletOpened/walletError).
    Q_INVOKABLE QString switchNetwork(int netType, const QString &password, double restoreHeight);
    Q_INVOKABLE void clearNewWalletSeed();

    // Helpers
    Q_INVOKABLE QString walletPath(const QString &dir, const QString &name) const;
    Q_INVOKABLE bool walletExists(const QString &path) const;
    Q_INVOKABLE QVariantList findWallets(const QString &dir) const;  // [{name, path, modified}]
    Q_INVOKABLE QString urlToLocalFile(const QUrl &url) const;
    Q_INVOKABLE QString localFileToUrl(const QString &path) const;
    Q_INVOKABLE bool addressValid(const QString &address, int netType) const;
    Q_INVOKABLE bool masterNodeKeyValid(const QString &key) const;
    Q_INVOKABLE bool amountValid(const QString &amount) const;
    Q_INVOKABLE double estimateHeightForDate(int netType, const QString &isoDate) const;
    Q_INVOKABLE QVariantMap parseUri(const QString &uri) const;  // beldex:<addr>?tx_amount=…
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE bool saveTextFile(const QUrl &url, const QString &text) const;
    Q_INVOKABLE QString shortAddress(const QString &address, int keep = 8) const;
    // For display only: paths inside the home folder are shown as ~/…
    Q_INVOKABLE QString prettyPath(const QString &path) const;

signals:
    void walletChanged();
    void busyChanged();
    void newWalletSeedChanged();
    void walletOpened();
    void walletClosed();
    void closingChanged();
    void walletError(const QString &message);

private:
    using Factory = std::function<std::unique_ptr<bdx::IWallet>(bdx::IEngine &)>;
    void runOpen(const QString &busyText, Factory f, bool captureSeed);
    void setBusy(bool b, const QString &text = {});

    std::unique_ptr<bdx::IEngine> m_engine;
    QPointer<Wallet> m_wallet;
    QThreadPool m_pool;
    int m_closing = 0;
    bool m_busy = false;
    QString m_busyText;
    QString m_newSeed;
};
