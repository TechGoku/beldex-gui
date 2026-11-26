// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
//
// Smoke test for whichever bdx::IEngine is linked in (real Beldex core or the demo engine).
//   engine_smoke [node host:port]
// Creates a wallet in a temp dir, reopens it, checks a wrong password is rejected, restores it
// from its seed (same address), checks helpers, and - if a node is given - connects and reads
// the network height. Exits non-zero on the first failure.

#include "engine/WalletEngine.h"
#include "libwalletqt/Amount.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, what)                                                                          \
    do {                                                                                           \
        const bool ok_ = (cond);                                                                   \
        std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", what);                                      \
        if (!ok_) ++failures;                                                                      \
    } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString node = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    auto engine = bdx::createEngine();
    std::printf("engine: %s\n", qPrintable(engine->name()));

    QTemporaryDir tmp;
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("smoke/smoke"));
    QDir().mkpath(QDir(tmp.path()).filePath(QStringLiteral("smoke")));
    const QString pw = QStringLiteral("correct horse");

    QString address, seed;
    {
        auto w = engine->createWallet(path, pw, QStringLiteral("English"), bdx::NetType::Mainnet);
        CHECK(w != nullptr, "create wallet");
        if (!w) {
            std::printf("      error: %s\n", qPrintable(engine->errorString()));
            return 1;
        }
        address = w->address(0, 0);
        seed = w->seed();
        std::printf("      address: %s\n", qPrintable(address));
        CHECK(address.startsWith(QStringLiteral("bx")), "mainnet address starts with bx");
        CHECK(seed.split(' ', Qt::SkipEmptyParts).size() == 25, "seed has 25 words");
        CHECK(w->secretViewKey().size() == 64 && w->publicSpendKey().size() == 64, "keys are 64 hex chars");
        CHECK(engine->addressValid(address, bdx::NetType::Mainnet), "own address validates on mainnet");
        CHECK(!engine->addressValid(address, bdx::NetType::Testnet), "own address rejected on testnet");
        w->addSubaddress(0, QStringLiteral("label"));
        CHECK(w->subaddresses(0).size() == 2, "subaddress created");
        CHECK(engine->closeWallet(std::move(w), true), "close + store");
    }
    CHECK(engine->walletExists(path), "walletExists");
    CHECK(engine->verifyPassword(path + QStringLiteral(".keys"), pw), "verifyPassword (right)");
    CHECK(!engine->verifyPassword(path + QStringLiteral(".keys"), QStringLiteral("nope")), "verifyPassword (wrong)");
    CHECK(engine->openWallet(path, QStringLiteral("nope"), bdx::NetType::Mainnet) == nullptr, "open with wrong password fails");
    {
        auto w = engine->openWallet(path, pw, bdx::NetType::Mainnet);
        CHECK(w && w->address(0, 0) == address, "reopen gives same address");
        CHECK(w && w->subaddresses(0).size() == 2, "subaddress persisted");
        if (w)
            engine->closeWallet(std::move(w), false);
    }
    {
        const QString rpath = QDir(tmp.path()).filePath(QStringLiteral("restored/restored"));
        QDir().mkpath(QDir(tmp.path()).filePath(QStringLiteral("restored")));
        auto w = engine->restoreFromSeed(rpath, pw, seed, bdx::NetType::Mainnet, 0, {});
        CHECK(w && w->address(0, 0) == address, "restore from seed gives same address");
        if (w)
            engine->closeWallet(std::move(w), false);
    }

    {
        // A testnet wallet opened as mainnet: what does core do?
        const QString tpath = QDir(tmp.path()).filePath(QStringLiteral("tn/tn"));
        QDir().mkpath(QDir(tmp.path()).filePath(QStringLiteral("tn")));
        auto t = engine->createWallet(tpath, pw, QStringLiteral("English"), bdx::NetType::Testnet);
        const QString taddr = t ? t->address(0, 0) : QString();
        std::printf("      testnet address: %s\n", qPrintable(taddr.left(12)));
        if (t)
            engine->closeWallet(std::move(t), true);
        auto wrong = engine->openWallet(tpath, pw, bdx::NetType::Mainnet);
        std::printf("      open testnet wallet as mainnet: %s, error: %s\n", wrong ? "opened" : "refused",
                    qPrintable(engine->errorString()));
        if (wrong) {
            std::printf("      address seen as mainnet: %s\n", qPrintable(wrong->address(0, 0).left(12)));
            engine->closeWallet(std::move(wrong), false);
        }
        auto right = engine->openWallet(tpath, pw, bdx::NetType::Testnet);
        CHECK(right && right->address(0, 0) == taddr, "testnet wallet opens as testnet");
        if (right) {
            // privacy-token reads on a wallet that holds none (no node: nothing to ask)
            CHECK(right->tokenBalances(0).empty(), "new testnet wallet has no token balances");
            const auto txs = right->history();
            CHECK(std::all_of(txs.begin(), txs.end(), [](const bdx::TxRecord &t) { return t.tokenId.isEmpty(); }),
                  "history reads with token ids");
            engine->closeWallet(std::move(right), false);
        }
    }

    CHECK(engine->amountFromString(QStringLiteral("1.5")).value_or(0) == 1500000000ULL, "amountFromString 1.5 BDX");
    CHECK(engine->masterNodeKeyValid(QString(64, QLatin1Char('a'))), "master node key validator");

    // privacy-token amounts use each token's own decimals (0..18)
    CHECK(bdx::parseUnits(QStringLiteral("123.45"), 4).value_or(0) == 1234500ULL, "parseUnits 123.45 @4");
    CHECK(bdx::parseUnits(QStringLiteral("7"), 0).value_or(0) == 7ULL, "parseUnits 7 @0");
    CHECK(!bdx::parseUnits(QStringLiteral("1.234"), 2), "parseUnits rejects extra decimals");
    CHECK(bdx::parseUnits(QStringLiteral("1.230"), 2).value_or(0) == 123ULL, "parseUnits ignores trailing zeros");
    CHECK(!bdx::parseUnits(QStringLiteral("18446744073709551616"), 0), "parseUnits rejects 2^64");
    CHECK(bdx::parseUnits(QStringLiteral("18.446744073709551615"), 18).value_or(0) == 18446744073709551615ULL, "parseUnits max @18");
    CHECK(!bdx::parseUnits(QStringLiteral("19"), 18), "parseUnits rejects overflow @18");
    CHECK(bdx::formatUnits(1234500, 4) == QStringLiteral("123.45"), "formatUnits 123.45");
    CHECK(bdx::formatUnits(100000000000ULL, 6, 6) == QStringLiteral("100,000.00"), "formatUnits keeps 2 decimals");
    CHECK(bdx::formatUnits(42, 0) == QStringLiteral("42"), "formatUnits 0 decimals");
    CHECK(bdx::formatUnits(5, 18, 18) == QStringLiteral("0.000000000000000005"), "formatUnits 18 decimals");

    if (!node.isEmpty()) {
        auto w = engine->openWallet(path, pw, bdx::NetType::Mainnet);
        w->setRefreshFromHeight(0);
        const bool init = w->init(node, {}, {}, false);
        CHECK(init, "init daemon");
        QElapsedTimer t;
        t.start();
        bdx::ConnStatus st = bdx::ConnStatus::Disconnected;
        while (t.elapsed() < 20000 && (st = w->connectionStatus()) == bdx::ConnStatus::Disconnected)
            QThread::msleep(500);
        CHECK(st == bdx::ConnStatus::Connected, "connected to node");
        const quint64 h = w->daemonHeight();
        std::printf("      %s height %llu, hard fork v%d\n", qPrintable(node), (unsigned long long)h,
                    w->hardForkVersion().value_or(0));
        CHECK(h > 5000000, "network height looks like mainnet");
        engine->closeWallet(std::move(w), false);
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
