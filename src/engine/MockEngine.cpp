// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
//
// DEMO ENGINE - builds the full UI without Beldex core (cmake -DBELDEX_GUI_MOCK=ON).
// Wallet "files" are small JSON documents holding a random fake address and seed; sync,
// history, stakes and BNS records are simulated. Nothing here touches the Beldex network,
// and nothing here is a usable key. The UI shows a DEMO banner whenever this engine is active.

#include "EngineCommon.h"
#include "WalletEngine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>
#include <atomic>
#include <functional>

namespace bdx {
namespace {

const char *kWords[] = {
    "abbey", "acid", "agenda", "aisle", "alpine", "amber", "anchor", "anvil", "apex", "arcade", "argue", "aspen",
    "atlas", "audio", "autumn", "avenue", "badge", "balcony", "bamboo", "banjo", "barrel", "basil", "beacon", "bench",
    "bison", "blanket", "bloom", "border", "bounce", "breeze", "bridge", "bronze", "bubble", "cabin", "cactus", "camera",
    "canyon", "carbon", "castle", "cedar", "cement", "chalk", "cherry", "cinema", "circle", "clover", "cobalt", "comet",
    "copper", "coral", "cotton", "crater", "cricket", "crystal", "dagger", "dawn", "delta", "denim", "desert", "diesel",
    "dolphin", "dragon", "drift", "eagle", "echo", "eclipse", "elbow", "ember", "engine", "epoch", "falcon", "fabric",
    "feather", "fiber", "flint", "fossil", "galaxy", "garnet", "gecko", "geyser", "ginger", "glacier", "granite", "gravel",
    "harbor", "hazel", "helium", "hollow", "horizon", "husky", "igloo", "indigo", "island", "ivory", "jacket", "jasmine",
    "jigsaw", "jungle", "kayak", "kernel", "kettle", "lagoon", "lantern", "lava", "lemon", "lilac", "lizard", "lotus",
    "magnet", "mango", "marble", "meadow", "meteor", "mirror", "mosaic", "nectar", "neon", "nickel", "nimbus", "nutmeg",
    "oasis", "olive", "onyx", "orbit", "orchid", "otter", "oxygen", "paddle", "pebble", "pepper", "pigeon", "pixel",
    "plasma", "polar", "prism", "pulse", "quartz", "quiver", "radar", "raven", "reef", "ribbon", "rocket", "saddle",
    "saffron", "salmon", "sapphire", "signal", "silver", "socket", "solar", "spiral", "sponge", "summit", "tango", "teapot",
    "thunder", "timber", "topaz", "tundra", "tunnel", "turtle", "umbra", "unicorn", "urchin", "velvet", "vertex", "violet",
    "walnut", "willow", "winter", "wizard", "yonder", "zenith", "zephyr", "zigzag",
};

const char kBase58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

constexpr quint64 kAnchorHeight = 5953938;
const qint64 kAnchorSecs = QDateTime(QDate(2026, 11, 24), QTime(10, 54), QTimeZone::UTC).toSecsSinceEpoch();

quint64 networkHeightNow()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return kAnchorHeight + quint64(std::max<qint64>(0, (now - kAnchorSecs) / 30));
}

QDateTime timeOfHeight(quint64 h)
{
    return QDateTime::fromSecsSinceEpoch(kAnchorSecs - (qint64(kAnchorHeight) - qint64(h)) * 30);
}

QString randomBase58(QRandomGenerator &rng, int n)
{
    QString out;
    for (int i = 0; i < n; ++i)
        out += QChar(kBase58[rng.bounded(int(sizeof(kBase58) - 1))]);
    return out;
}

QString randomHex(QRandomGenerator &rng, int bytes)
{
    QByteArray b(bytes, 0);
    for (auto &c : b)
        c = char(rng.bounded(256));
    return QString::fromLatin1(b.toHex());
}

QString prefixFor(NetType net) { return net == NetType::Mainnet ? QStringLiteral("bx") : QStringLiteral("9t"); }

QString fakeAddress(QRandomGenerator &rng, NetType net) { return prefixFor(net) + randomBase58(rng, 95); }

QByteArray pwHash(const QString &pw, const QString &salt)
{
    return QCryptographicHash::hash((salt + pw).toUtf8(), QCryptographicHash::Sha256).toHex();
}

// ---------------------------------------------------------------------------------------------
class MockPendingTx final : public IPendingTx {
public:
    using CommitFn = std::function<bool(MockPendingTx &, bool)>;
    MockPendingTx(QString err) : m_error(std::move(err)) {}
    MockPendingTx(quint64 amount, quint64 fee, CommitFn fn) : m_amount(amount), m_fee(fee), m_commit(std::move(fn))
    {
        auto rng = QRandomGenerator::securelySeeded();
        m_txid = randomHex(rng, 32);
    }
    bool ok() const override { return m_error.isEmpty(); }
    QString errorString() const override { return m_error; }
    quint64 amount() const override { return m_amount; }
    quint64 fee() const override { return m_fee; }
    quint64 txCount() const override { return ok() ? 1 : 0; }
    QStringList txids() const override { return ok() ? QStringList{m_txid} : QStringList{}; }
    bool commit(bool flash) override
    {
        if (!ok() || !m_commit)
            return false;
        auto fn = std::move(m_commit);
        return fn(*this, flash);
    }
    QString m_error;
    quint64 m_amount = 0, m_fee = 0;
    QString m_txid;
    CommitFn m_commit;
};

// ---------------------------------------------------------------------------------------------
class MockWallet final : public IWallet {
public:
    MockWallet(QString path, QString password, QJsonObject doc) : m_path(std::move(path)), m_password(std::move(password))
    {
        m_net = NetType(doc.value("net").toInt());
        m_seed = doc.value("seed").toString();
        m_restoreHeight = quint64(doc.value("restoreHeight").toDouble());
        m_rng.seed(qHash(m_seed));
        m_primary = doc.value("address").toString();
        m_keys = {randomHex(m_rng, 32), randomHex(m_rng, 32), randomHex(m_rng, 32), randomHex(m_rng, 32)};
        for (const auto &a : doc.value("accounts").toArray())
            m_accountLabels << a.toString();
        if (m_accountLabels.isEmpty())
            m_accountLabels << QStringLiteral("Primary account");
        for (const auto &v : doc.value("subaddresses").toArray()) {
            const auto o = v.toObject();
            m_subs.push_back({quint32(o.value("account").toInt()), quint32(o.value("index").toInt()),
                              o.value("address").toString(), o.value("label").toString()});
        }
        for (const auto &v : doc.value("book").toArray()) {
            const auto o = v.toObject();
            m_book.push_back({int(m_book.size()), o.value("address").toString(), o.value("description").toString()});
        }
        const QJsonObject notes = doc.value("notes").toObject();
        for (auto it = notes.begin(); it != notes.end(); ++it)
            m_notes.insert(it.key(), it.value().toString());
        if (doc.value("demoHistory").toBool())
            generateHistory();
        for (const auto &v : doc.value("sent").toArray())
            m_txs.push_back(txFromJson(v.toObject()));
        if (m_net == NetType::Testnet && doc.value("demoHistory").toBool())
            seedDemoTokens();
    }

    // -- identity -----------------------------------------------------------------------------
    bool ok() const override { return m_error.isEmpty(); }
    QString errorString() const override { return m_error; }
    QString seed() const override { return m_seed; }
    QString seedLanguage() const override { return QStringLiteral("English"); }
    QString address(quint32 a, quint32 i) const override
    {
        if (a == 0 && i == 0)
            return m_primary;
        for (const auto &sub : m_subs)
            if (sub.account == a && sub.index == i)
                return sub.address;
        return {};
    }
    QString path() const override { return m_path; }
    NetType netType() const override { return m_net; }
    QString secretViewKey() const override { return m_keys[0]; }
    QString publicViewKey() const override { return m_keys[1]; }
    QString secretSpendKey() const override { return m_keys[2]; }
    QString publicSpendKey() const override { return m_keys[3]; }
    bool watchOnly() const override { return false; }
    bool setPassword(const QString &pw) override
    {
        m_password = pw;
        return store();
    }
    bool store() override
    {
        QMutexLocker l(&m_mutex);
        QJsonObject doc = readDoc(m_path);
        const QString salt = randomHex(m_rng, 8);
        doc["salt"] = salt;
        doc["pw"] = QString::fromLatin1(pwHash(m_password, salt));
        QJsonArray accounts;
        for (const auto &a : m_accountLabels)
            accounts << a;
        doc["accounts"] = accounts;
        QJsonArray subs;
        for (const auto &sub : m_subs)
            subs << QJsonObject{{"account", int(sub.account)}, {"index", int(sub.index)}, {"address", sub.address}, {"label", sub.label}};
        doc["subaddresses"] = subs;
        QJsonArray book;
        for (const auto &b : m_book)
            book << QJsonObject{{"address", b.address}, {"description", b.description}};
        doc["book"] = book;
        QJsonObject notes;
        for (auto it = m_notes.begin(); it != m_notes.end(); ++it)
            notes[it.key()] = it.value();
        doc["notes"] = notes;
        QJsonArray sent;
        for (const auto &t : m_txs)
            if (t.sentByUser)
                sent << txToJson(t);
        doc["sent"] = sent;
        return writeDoc(m_path, doc);
    }

    // -- sync ---------------------------------------------------------------------------------
    bool init(const QString &daemon, const QString &, const QString &, bool) override
    {
        m_daemon = daemon;
        m_connected = !daemon.trimmed().isEmpty();
        return m_connected;
    }
    ConnStatus connectionStatus() const override { return m_connected ? ConnStatus::Connected : ConnStatus::Disconnected; }
    void setRefreshFromHeight(quint64 h) override { m_restoreHeight = h; }
    void setRecovering(bool) override {}
    bool setAttribute(const QString &k, const QString &v) override { m_attrs.insert(k, v); return true; }
    QString attribute(const QString &k) const override { return m_attrs.value(k); }
    quint64 refreshFromHeight() const override { return m_restoreHeight; }
    void startRefresh() override
    {
        if (!m_syncClock.isValid())
            m_syncClock.start();
        m_refreshing = true;
    }
    void pauseRefresh() override { m_refreshing = false; }
    void refreshAsync() override { startRefresh(); }
    void rescanAsync() override { m_syncClock.restart(); m_refreshing = true; }
    bool synchronized() const override { return blockHeight() >= daemonHeight(); }
    bool isRefreshing() override { return m_refreshing && m_connected && !synchronized(); }
    void stopScan() override {}
    quint64 blockHeight() const override
    {
        if (!m_connected || !m_syncClock.isValid())
            return m_restoreHeight;
        // ~12 seconds for a full simulated sync, however far back the restore height is.
        const quint64 target = daemonHeight();
        const quint64 span = target > m_restoreHeight ? target - m_restoreHeight : 0;
        const quint64 done = std::min<quint64>(span, span * quint64(m_syncClock.elapsed()) / 12000 + 1);
        return m_restoreHeight + done;
    }
    quint64 daemonHeight() const override { return m_connected ? networkHeightNow() : 0; }
    quint64 daemonTargetHeight() const override { return daemonHeight(); }
    std::optional<int> hardForkVersion() const override
    {
        if (!m_connected)
            return std::nullopt;
        return 21;
    }
    void setListener(IWalletListener *l) override { m_listener = l; }

    // -- balances -----------------------------------------------------------------------------
    quint64 balance(quint32 a) const override { return computeBalance(a, false); }
    quint64 unlockedBalance(quint32 a) const override { return computeBalance(a, true); }

    std::vector<AccountRecord> accounts() override
    {
        std::vector<AccountRecord> out;
        for (int i = 0; i < m_accountLabels.size(); ++i)
            out.push_back({quint32(i), address(quint32(i), 0), m_accountLabels[i], balance(quint32(i)), unlockedBalance(quint32(i))});
        return out;
    }
    void addAccount(const QString &label) override
    {
        const quint32 idx = quint32(m_accountLabels.size());
        m_accountLabels << label;
        m_subs.push_back({idx, 0, prefixFor(m_net) + randomBase58(m_rng, 95), label});
        store();
    }
    void setAccountLabel(quint32 a, const QString &label) override
    {
        if (int(a) < m_accountLabels.size())
            m_accountLabels[int(a)] = label;
        store();
    }
    std::vector<SubaddressRecord> subaddresses(quint32 a) override
    {
        std::vector<SubaddressRecord> out;
        if (a == 0)
            out.push_back({0, m_primary, m_accountLabels.value(0)});
        for (const auto &sub : m_subs)
            if (sub.account == a)
                out.push_back({sub.index, sub.address, sub.label});
        std::sort(out.begin(), out.end(), [](auto &x, auto &y) { return x.index < y.index; });
        return out;
    }
    void addSubaddress(quint32 a, const QString &label) override
    {
        quint32 next = a == 0 ? 1 : 0;
        for (const auto &sub : m_subs)
            if (sub.account == a)
                next = std::max(next, sub.index + 1);
        m_subs.push_back({a, next, QStringLiteral("bX") + randomBase58(m_rng, 95), label});
        store();
    }
    void setSubaddressLabel(quint32 a, quint32 i, const QString &label) override
    {
        if (a == 0 && i == 0) {
            m_accountLabels[0] = label;
        } else {
            for (auto &sub : m_subs)
                if (sub.account == a && sub.index == i)
                    sub.label = label;
        }
        store();
    }

    // -- history ------------------------------------------------------------------------------
    std::vector<TxRecord> history() override
    {
        std::vector<TxRecord> out;
        const quint64 h = blockHeight(), top = daemonHeight();
        for (const auto &t : m_txs) {
            if (!t.rec.pending && t.rec.blockHeight > h)
                continue;  // not scanned yet
            TxRecord r = t.rec;
            if (!r.pending)
                r.confirmations = top >= r.blockHeight ? top - r.blockHeight + 1 : 0;
            else if (top > t.rec.blockHeight + 1) {  // "mined" after ~1 block
                r.pending = false;
                r.confirmations = top - t.rec.blockHeight;
            }
            r.description = m_notes.value(r.hash);
            out.push_back(std::move(r));
        }
        std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.timestamp > b.timestamp; });
        return out;
    }
    bool setTxNote(const QString &txid, const QString &note) override
    {
        m_notes.insert(txid, note);
        return store();
    }
    QString txKey(const QString &txid) const override
    {
        return QString::fromLatin1(QCryptographicHash::hash(txid.toUtf8() + m_seed.toUtf8(), QCryptographicHash::Sha256).toHex());
    }
    std::vector<AddressBookRecord> addressBook() override { return m_book; }
    bool addAddressBookRow(const QString &a, const QString &d) override
    {
        if (!addrOk(a)) {
            m_error = QStringLiteral("Invalid address");
            return false;
        }
        m_error.clear();
        m_book.push_back({int(m_book.size()), a, d});
        return store();
    }
    bool deleteAddressBookRow(int rowId) override
    {
        m_book.erase(std::remove_if(m_book.begin(), m_book.end(), [&](auto &r) { return r.rowId == rowId; }), m_book.end());
        for (size_t i = 0; i < m_book.size(); ++i)
            m_book[i].rowId = int(i);
        return store();
    }

    // -- sending ------------------------------------------------------------------------------
    std::unique_ptr<IPendingTx> createTransaction(const QStringList &dests, const QList<quint64> &amounts,
                                                  uint32_t priority, quint32 account, const QString &tokenId) override
    {
        if (!tokenId.isEmpty())
            return tokenTransfer(dests, amounts, priority, account, tokenId);
        if (!m_connected)
            return std::make_unique<MockPendingTx>(QStringLiteral("No connection to daemon"));
        if (!synchronized())
            return std::make_unique<MockPendingTx>(QStringLiteral("Wallet is not synchronized yet"));
        for (const auto &d : dests)
            if (!addrOk(d))
                return std::make_unique<MockPendingTx>(QStringLiteral("Invalid destination address: %1").arg(d.left(12) + QStringLiteral("…")));
        quint64 total = 0;
        for (auto a : amounts)
            total += a;
        const quint64 fee = estimateFee(priority, quint32(dests.size()));
        if (total == 0)
            return std::make_unique<MockPendingTx>(QStringLiteral("Amount must be greater than zero"));
        if (total + fee > unlockedBalance(account))
            return std::make_unique<MockPendingTx>(QStringLiteral("Not enough unlocked balance (need %1 BDX incl. fee)")
                                                       .arg(double(total + fee) / kAtomicPerBdx, 0, 'f', 4));
        return outgoing(total, fee, account, dests, TxRecord::Kind::Transfer);
    }
    std::unique_ptr<IPendingTx> createSweepAll(uint32_t priority, quint32 account) override
    {
        const quint64 fee = estimateFee(priority, 1);
        const quint64 bal = unlockedBalance(account);
        if (bal <= fee)
            return std::make_unique<MockPendingTx>(QStringLiteral("Nothing to sweep"));
        return outgoing(bal - fee, fee, account, {m_primary}, TxRecord::Kind::Transfer);
    }
    std::unique_ptr<IPendingTx> sweep(const QString &what, const QString &address, uint32_t priority,
                                      quint32 account) override
    {
        if (!address.isEmpty() && !addrOk(address))
            return std::make_unique<MockPendingTx>(QStringLiteral("Invalid destination address"));
        const bool away = !address.isEmpty() && address != m_primary;
        const QString to = address.isEmpty() ? m_primary : address;
        const quint64 fee = estimateFee(priority, 1);
        if (what == QLatin1String("bdx") || what == QLatin1String("all")) {
            const quint64 bal = unlockedBalance(account);
            if (bal <= fee)
                return std::make_unique<MockPendingTx>(QStringLiteral("Nothing to sweep"));
            auto tx = outgoing(away ? bal - fee : 0, fee, account, {to}, TxRecord::Kind::Transfer);
            if (what == QLatin1String("all") && away) {
                auto *p = static_cast<MockPendingTx *>(tx.get());
                auto inner = std::move(p->m_commit);
                p->m_commit = [this, inner, account](MockPendingTx &t, bool flash) {
                    for (auto it = m_tokenBal.begin(); it != m_tokenBal.end();)
                        it = it.key().first == account ? m_tokenBal.erase(it) : std::next(it);
                    return inner(t, flash);
                };
            }
            return tx;
        }
        const quint64 have = m_tokenBal.value({account, what});
        if (!have)
            return std::make_unique<MockPendingTx>(QStringLiteral("No unlocked balance of that token"));
        if (fee > unlockedBalance(account))
            return std::make_unique<MockPendingTx>(QStringLiteral("Not enough BDX for the fee"));
        return tokenTx(have, fee, account, {to}, TxRecord::Kind::Transfer, what, [this, account, what, away] {
            if (away)
                m_tokenBal.remove({account, what});
        });
    }

    // -- privacy tokens (demo: in memory only) -----------------------------------------------
    std::vector<TokenBalance> tokenBalances(quint32 account) override
    {
        std::vector<TokenBalance> out;
        for (auto it = m_tokenBal.begin(); it != m_tokenBal.end(); ++it)
            if (it.key().first == account && it.value() > 0) {
                const TokenInfo &t = m_tokens[it.key().second];
                out.push_back({t.tokenId, t.ticker, it.value(), it.value(), t.decimals});
            }
        return out;
    }
    std::vector<TokenInfo> knownTokens() override
    {
        std::vector<TokenInfo> out;
        for (const auto &t : m_tokens)
            out.push_back(t);
        return out;
    }
    std::unique_ptr<IPendingTx> registerToken(const QString &json, QString *tokenId, uint32_t priority,
                                              quint32 account) override
    {
        const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
        TokenInfo t;
        t.ticker = o.value("ticker").toString();
        t.fullName = o.value("full_name").toString();
        t.maxSupply = quint64(o.value("total_max_supply").toDouble());
        t.supply = quint64(o.value("current_supply").toDouble());
        t.decimals = o.value("decimal_point").toInt();
        t.metaInfo = o.value("meta_info").toString();
        t.owner = publicSpendKey();
        if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9]{1,14}$")).match(t.ticker).hasMatch())
            return std::make_unique<MockPendingTx>(QStringLiteral("Invalid token descriptor JSON: ticker is invalid; expected 1-14 ASCII alphanumeric characters"));
        if (t.fullName.isEmpty() || t.maxSupply == 0 || t.supply > t.maxSupply)
            return std::make_unique<MockPendingTx>(QStringLiteral("Invalid token descriptor JSON: check name and supply"));
        const quint64 fee = estimateFee(priority, 1);
        if (fee > unlockedBalance(account))
            return std::make_unique<MockPendingTx>(QStringLiteral("Not enough BDX for the fee"));
        t.tokenId = randomHex(m_rng, 32);
        if (tokenId)
            *tokenId = t.tokenId;
        return tokenTx(t.supply, fee, account, {m_primary}, TxRecord::Kind::TokenRegister, t.tokenId, [this, t, account] {
            m_tokens.insert(t.tokenId, t);
            if (t.supply)
                m_tokenBal[{account, t.tokenId}] += t.supply;
        });
    }
    std::unique_ptr<IPendingTx> mintToken(const QString &id, quint64 amount, uint32_t priority, quint32 account) override
    {
        if (auto err = ownedCheck(id); !err.isEmpty())
            return std::make_unique<MockPendingTx>(err);
        const TokenInfo &t = m_tokens[id];
        if (amount == 0 || amount > t.maxSupply - t.supply)
            return std::make_unique<MockPendingTx>(QStringLiteral("Mint amount exceeds the remaining supply"));
        return tokenTx(amount, estimateFee(priority, 1), account, {m_primary}, TxRecord::Kind::TokenMint, id,
                       [this, id, amount, account] {
                           m_tokens[id].supply += amount;
                           m_tokenBal[{account, id}] += amount;
                       });
    }
    std::unique_ptr<IPendingTx> burnToken(const QString &id, quint64 amount, uint32_t priority, quint32 account) override
    {
        if (!m_tokens.contains(id))
            return std::make_unique<MockPendingTx>(QStringLiteral("Invalid token id"));
        if (amount == 0 || amount > m_tokenBal.value({account, id}))
            return std::make_unique<MockPendingTx>(QStringLiteral("Not enough of that token to burn"));
        return tokenTx(amount, estimateFee(priority, 1), account, {}, TxRecord::Kind::TokenBurn, id,
                       [this, id, amount, account] {
                           m_tokens[id].supply -= amount;
                           m_tokenBal[{account, id}] -= amount;
                       });
    }
    std::unique_ptr<IPendingTx> updateToken(const QString &id, const QString &json, uint32_t priority) override
    {
        if (auto err = ownedCheck(id); !err.isEmpty())
            return std::make_unique<MockPendingTx>(err);
        const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
        const QString meta = o.contains("meta_info") ? o.value("meta_info").toString() : m_tokens[id].metaInfo;
        const QString owner = o.value("owner").toString();
        if (meta == m_tokens[id].metaInfo && owner.isEmpty())
            return std::make_unique<MockPendingTx>(QStringLiteral("update_token: meta_info and owner are unchanged, nothing to update"));
        if (!owner.isEmpty() && !addrOk(owner))
            return std::make_unique<MockPendingTx>(QStringLiteral("Invalid token descriptor JSON: owner must be a hex-encoded public key or valid address"));
        return tokenTx(0, estimateFee(priority, 1), 0, {}, TxRecord::Kind::TokenUpdate, id, [this, id, meta, owner] {
            m_tokens[id].metaInfo = meta;
            if (!owner.isEmpty() && owner != m_primary)
                m_tokens[id].owner = randomHex(m_rng, 32);  // demo: some other wallet's spend key
        });
    }

    quint64 estimateFee(uint32_t priority, quint32 recipients) const override
    {
        // base fee 0.0001 BDX / output + 0.006666 BDX / kB (explorer "network parameters"), flash x3
        const quint64 perOut = 100000, perKb = 6666000;
        quint64 f = perOut * (recipients + 1) + perKb * (1 + recipients / 2) * 13 / 10;
        return priority == PriorityFlash ? f * 3 : f;
    }

    // -- staking ------------------------------------------------------------------------------
    std::vector<StakeRecord> stakes() override { return m_stakes; }
    std::unique_ptr<IPendingTx> stake(const QString &mnKey, quint64 amount) override
    {
        if (!QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(mnKey).hasMatch())
            return std::make_unique<MockPendingTx>(QStringLiteral("Invalid master node public key"));
        if (amount + estimateFee(0, 1) > unlockedBalance(0))
            return std::make_unique<MockPendingTx>(QStringLiteral("Not enough unlocked balance to stake"));
        auto tx = outgoing(0, estimateFee(0, 1), 0, {m_primary}, TxRecord::Kind::Stake);
        auto *p = static_cast<MockPendingTx *>(tx.get());
        auto inner = std::move(p->m_commit);
        p->m_amount = amount;
        p->m_commit = [this, inner, mnKey, amount](MockPendingTx &t, bool flash) {
            m_stakes.push_back({mnKey.toLower(), amount, std::nullopt, true, false});
            m_lockedByStake += amount;
            return inner(t, flash);
        };
        return tx;
    }
    StakeUnlockResult canRequestStakeUnlock(const QString &mnKey) override
    {
        StakeUnlockResult r;
        for (const auto &s : m_stakes)
            if (s.masterNodeKey.compare(mnKey, Qt::CaseInsensitive) == 0) {
                r.success = !s.unlockHeight;
                r.message = s.unlockHeight
                                ? QStringLiteral("Unlock already requested; funds unlock at height %1").arg(*s.unlockHeight)
                                : QStringLiteral("Stake can be unlocked (demo: 43200 blocks).");
                return r;
            }
        r.message = QStringLiteral("No stake from this wallet in that master node");
        return r;
    }
    StakeUnlockResult requestStakeUnlock(const QString &mnKey) override
    {
        StakeUnlockResult r = canRequestStakeUnlock(mnKey);
        if (!r.success)
            return r;
        r.message = QStringLiteral("Unlock request ready to broadcast");
        auto tx = std::make_unique<MockPendingTx>(0, estimateFee(0, 1), [this, mnKey](MockPendingTx &, bool) {
            for (auto &s : m_stakes)
                if (s.masterNodeKey.compare(mnKey, Qt::CaseInsensitive) == 0)
                    s.unlockHeight = daemonHeight() + 43200;
            return true;
        });
        r.tx = std::move(tx);
        return r;
    }

    // -- BNS ----------------------------------------------------------------------------------
    std::vector<BnsRecord> bnsRecords() override { return m_bns; }
    std::unique_ptr<IPendingTx> buyBns(const BnsRequest &req, quint32 account) override
    {
        const QString name = req.name.trimmed().toLower();
        if (!QRegularExpression(QStringLiteral("^[a-z0-9]([a-z0-9-]{0,62}[a-z0-9])?\\.bdx$")).match(name).hasMatch())
            return std::make_unique<MockPendingTx>(QStringLiteral("Name must look like \"example.bdx\" (a-z, 0-9, hyphen)"));
        for (const auto &b : m_bns)
            if (b.name == name)
                return std::make_unique<MockPendingTx>(QStringLiteral("You already own %1").arg(name));
        if (req.bchat.isEmpty() && req.wallet.isEmpty() && req.belnet.isEmpty() && req.eth.isEmpty())
            return std::make_unique<MockPendingTx>(QStringLiteral("Set at least one value (BChat ID, wallet, BelNet or ETH address)"));
        const quint64 burn = bnsPrice(req.years);
        if (!burn)
            return std::make_unique<MockPendingTx>(QStringLiteral("Unsupported BNS year"));
        if (burn + estimateFee(0, 1) > unlockedBalance(account))
            return std::make_unique<MockPendingTx>(QStringLiteral("Not enough unlocked balance"));
        auto tx = outgoing(burn, estimateFee(0, 1), account, {}, TxRecord::Kind::Bns);
        auto *p = static_cast<MockPendingTx *>(tx.get());
        auto inner = std::move(p->m_commit);
        p->m_commit = [this, inner, req, name](MockPendingTx &t, bool flash) {
            BnsRecord b;
            b.name = name;
            b.nameHash = QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Blake2b_256).toBase64());
            b.owner = req.owner.isEmpty() ? m_primary : req.owner;
            b.backupOwner = req.backupOwner;
            b.bchat = req.bchat;
            b.wallet = req.wallet;
            b.belnet = req.belnet;
            b.eth = req.eth;
            b.updateHeight = daemonHeight();
            b.expirationHeight = daemonHeight() + yearsToBlocks(req.years);
            m_bns.push_back(b);
            return inner(t, flash);
        };
        return tx;
    }
    std::unique_ptr<IPendingTx> renewBns(const QString &name, const QString &years, quint32 account) override
    {
        const quint64 burn = bnsPrice(years);
        if (!burn)
            return std::make_unique<MockPendingTx>(QStringLiteral("Unsupported BNS year"));
        auto tx = outgoing(burn, estimateFee(0, 1), account, {}, TxRecord::Kind::Bns);
        auto *p = static_cast<MockPendingTx *>(tx.get());
        auto inner = std::move(p->m_commit);
        p->m_commit = [this, inner, name, years](MockPendingTx &t, bool flash) {
            for (auto &b : m_bns)
                if (b.name == name)
                    b.expirationHeight += yearsToBlocks(years);
            return inner(t, flash);
        };
        return tx;
    }
    std::unique_ptr<IPendingTx> updateBns(const BnsRequest &req, quint32 account) override
    {
        auto tx = outgoing(0, estimateFee(0, 1), account, {}, TxRecord::Kind::Bns);
        auto *p = static_cast<MockPendingTx *>(tx.get());
        auto inner = std::move(p->m_commit);
        p->m_commit = [this, inner, req](MockPendingTx &t, bool flash) {
            for (auto &b : m_bns)
                if (b.name == req.name) {
                    if (!req.owner.isEmpty()) b.owner = req.owner;
                    if (!req.backupOwner.isEmpty()) b.backupOwner = req.backupOwner;
                    if (!req.bchat.isEmpty()) b.bchat = req.bchat;
                    if (!req.wallet.isEmpty()) b.wallet = req.wallet;
                    if (!req.belnet.isEmpty()) b.belnet = req.belnet;
                    if (!req.eth.isEmpty()) b.eth = req.eth;
                    b.updateHeight = daemonHeight();
                }
            return inner(t, flash);
        };
        return tx;
    }

    // -- tools --------------------------------------------------------------------------------
    QString signMessage(const QString &m) override
    {
        return QStringLiteral("SigV2") + QString::fromLatin1(QCryptographicHash::hash((m_seed + m).toUtf8(), QCryptographicHash::Sha512).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    }
    bool verifySignedMessage(const QString &m, const QString &a, const QString &sig) const override
    {
        if (a != m_primary)
            return false;
        return sig == QStringLiteral("SigV2") + QString::fromLatin1(QCryptographicHash::hash((m_seed + m).toUtf8(), QCryptographicHash::Sha512).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    }

    static QJsonObject readDoc(const QString &path)
    {
        QFile f(path + QStringLiteral(".keys"));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    }
    static bool writeDoc(const QString &path, const QJsonObject &doc)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path + QStringLiteral(".keys"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(QJsonDocument(doc).toJson());
        QFile w(path);  // the "cache" file, so file pickers see a wallet
        if (w.open(QIODevice::WriteOnly | QIODevice::Truncate))
            w.write("beldex-gui demo wallet cache\n");
        return true;
    }

private:
    struct MockTx {
        TxRecord rec;
        bool sentByUser = false;
        bool locksAmount = false;  // stake output stays locked
    };
    struct Sub {
        quint32 account, index;
        QString address, label;
    };

    bool addrOk(const QString &a) const { return a.startsWith(prefixFor(m_net), Qt::CaseInsensitive) && a.size() >= 95; }

    static quint64 yearsToBlocks(const QString &y)
    {
        const int n = y.left(y.size() - 1).toInt();
        return quint64(n) * 365 * 2880;
    }
    static quint64 bnsPrice(const QString &y)
    {
        if (y == "1y") return 650 * kAtomicPerBdx;
        if (y == "2y") return 1000 * kAtomicPerBdx;
        if (y == "5y") return 2000 * kAtomicPerBdx;
        if (y == "10y") return 4000 * kAtomicPerBdx;
        return 0;
    }

    quint64 computeBalance(quint32 account, bool unlockedOnly) const
    {
        const quint64 h = blockHeight();
        qint64 bal = 0;
        for (const auto &t : m_txs) {
            const auto &r = t.rec;
            if (r.account != account || r.failed)
                continue;
            if (!r.pending && r.blockHeight > h)
                continue;
            if (r.incoming) {
                const bool unlocked = !r.pending && h >= r.blockHeight + 10;
                if (!unlockedOnly || unlocked)
                    bal += qint64(r.amount);
            } else {
                bal -= qint64((r.tokenId.isEmpty() ? r.amount : 0) + r.fee);  // token amounts aren't BDX
            }
        }
        if (account == 0 && unlockedOnly)
            bal -= qint64(m_lockedByStake);
        return bal > 0 ? quint64(bal) : 0;
    }

    std::unique_ptr<IPendingTx> outgoing(quint64 amount, quint64 fee, quint32 account, QStringList dests, TxRecord::Kind kind)
    {
        return std::make_unique<MockPendingTx>(amount, fee, [this, account, dests, kind](MockPendingTx &t, bool flash) {
            MockTx tx;
            tx.sentByUser = true;
            tx.rec.hash = t.m_txid;
            tx.rec.incoming = false;
            tx.rec.pending = !flash;
            tx.rec.kind = kind;
            tx.rec.amount = t.m_amount;
            tx.rec.fee = t.m_fee;
            tx.rec.blockHeight = daemonHeight();
            tx.rec.timestamp = QDateTime::currentDateTime();
            tx.rec.account = account;
            tx.rec.destinations = dests;
            if (kind == TxRecord::Kind::Stake) {
                tx.rec.amount = 0;  // a stake is a transfer to yourself that gets locked
            }
            m_txs.push_back(tx);
            store();
            if (m_listener)
                m_listener->moneySpent(t.m_txid, t.m_amount);
            return true;
        });
    }

    QString ownedCheck(const QString &id) const
    {
        if (!m_tokens.contains(id))
            return QStringLiteral("Invalid token id");
        if (m_tokens.value(id).owner != publicSpendKey())
            return QStringLiteral("This wallet does not own the token: ") + id;
        return {};
    }

    // A token operation: BDX pays the fee, `apply` changes token state once broadcast.
    std::unique_ptr<IPendingTx> tokenTx(quint64 amount, quint64 fee, quint32 account, QStringList dests,
                                        TxRecord::Kind kind, QString tokenId, std::function<void()> apply)
    {
        return std::make_unique<MockPendingTx>(amount, fee, [=](MockPendingTx &t, bool flash) {
            apply();
            MockTx tx;  // not saved: demo token state lives in memory only
            tx.rec.hash = t.m_txid;
            tx.rec.pending = !flash;
            tx.rec.kind = kind;
            tx.rec.amount = amount;
            tx.rec.fee = fee;
            tx.rec.blockHeight = daemonHeight();
            tx.rec.timestamp = QDateTime::currentDateTime();
            tx.rec.account = account;
            tx.rec.destinations = dests;
            tx.rec.tokenId = tokenId;
            m_txs.push_back(tx);
            if (m_listener)
                m_listener->moneySpent(t.m_txid, fee);
            return true;
        });
    }

    std::unique_ptr<IPendingTx> tokenTransfer(const QStringList &dests, const QList<quint64> &amounts, uint32_t priority,
                                              quint32 account, const QString &id)
    {
        if (!m_tokens.contains(id))
            return std::make_unique<MockPendingTx>(QStringLiteral("Invalid token id"));
        quint64 total = 0;
        for (auto a : amounts)
            total += a;
        for (const auto &d : dests)
            if (!addrOk(d))
                return std::make_unique<MockPendingTx>(QStringLiteral("Invalid destination address: %1").arg(d.left(12) + QStringLiteral("…")));
        if (total == 0 || total > m_tokenBal.value({account, id}))
            return std::make_unique<MockPendingTx>(QStringLiteral("Not enough unlocked %1").arg(m_tokens[id].ticker));
        const quint64 fee = estimateFee(priority, quint32(dests.size()));
        if (fee > unlockedBalance(account))
            return std::make_unique<MockPendingTx>(QStringLiteral("Not enough BDX for the fee"));
        quint64 away = 0;  // sending to ourselves keeps the tokens
        for (int i = 0; i < dests.size(); ++i)
            if (dests[i] != m_primary)
                away += amounts[i];
        return tokenTx(total, fee, account, dests, TxRecord::Kind::Transfer, id, [this, account, id, away] {
            m_tokenBal[{account, id}] -= away;
        });
    }

    void seedDemoTokens()
    {
        QRandomGenerator rng(qHash(m_seed) ^ 0x70C3);
        const quint64 m6 = 1000000;
        TokenInfo own{randomHex(rng, 32), QStringLiteral("BLINK"), QStringLiteral("Blink Rewards"), publicSpendKey(),
                      1000000 * m6, 250000 * m6, 6, QStringLiteral("Loyalty points for the demo store")};
        TokenInfo other{randomHex(rng, 32), QStringLiteral("TUSD"), QStringLiteral("Test Dollar"), randomHex(rng, 32),
                        50000000ULL * 100, 12000000ULL * 100, 2, QString()};
        m_tokens.insert(own.tokenId, own);
        m_tokens.insert(other.tokenId, other);
        m_tokenBal[{0, own.tokenId}] = 180000 * m6;
        m_tokenBal[{0, other.tokenId}] = 1250 * 100;
        const quint64 top = networkHeightNow();
        auto rec = [&](quint64 h, bool in, TxRecord::Kind kind, quint64 amount, const QString &id) {
            MockTx t;
            t.rec.hash = randomHex(rng, 32);
            t.rec.incoming = in;
            t.rec.kind = kind;
            t.rec.amount = amount;
            t.rec.fee = in ? 0 : 41000000;
            t.rec.blockHeight = h;
            t.rec.timestamp = timeOfHeight(h);
            t.rec.confirmations = top - h;
            t.rec.tokenId = id;
            if (!in)
                t.rec.destinations << m_primary;
            m_txs.push_back(t);
        };
        rec(top - 2880 * 9, false, TxRecord::Kind::TokenRegister, 250000 * m6, own.tokenId);
        rec(top - 2880 * 4, false, TxRecord::Kind::Transfer, 70000 * m6, own.tokenId);
        rec(top - 2880 * 2, true, TxRecord::Kind::Transfer, 1250 * 100, other.tokenId);
    }

    void generateHistory()
    {
        QRandomGenerator rng(qHash(m_seed) ^ 0xB1D3);
        const quint64 top = networkHeightNow();
        const quint64 start = std::max<quint64>(m_restoreHeight + 100, top - 2880 * 75);
        auto mk = [&](quint64 h, bool in, TxRecord::Kind kind, quint64 amount, quint64 fee) {
            MockTx t;
            t.rec.hash = randomHex(rng, 32);
            t.rec.incoming = in;
            t.rec.kind = kind;
            t.rec.amount = amount;
            t.rec.fee = fee;
            t.rec.blockHeight = h;
            t.rec.timestamp = timeOfHeight(h);
            t.rec.unlockTime = kind == TxRecord::Kind::MasterNodeReward ? h + 30 : 0;
            if (!in)
                t.rec.destinations << fakeAddress(rng, m_net);
            m_txs.push_back(t);
        };
        // funding
        mk(start + 40, true, TxRecord::Kind::Transfer, 18250 * kAtomicPerBdx, 0);
        mk(start + 2880 * 3, true, TxRecord::Kind::Transfer, 4200500000000ULL, 0);
        // stake into a master node
        const QString mn = randomHex(rng, 32);
        mk(start + 2880 * 5, false, TxRecord::Kind::Stake, 0, 27300000);
        m_stakes.push_back({mn, 10000 * kAtomicPerBdx, std::nullopt, false, false});
        m_lockedByStake = 10000 * kAtomicPerBdx;
        // master node rewards, roughly daily
        for (quint64 h = start + 2880 * 6; h < top - 100; h += 2880 + rng.bounded(900))
            mk(h, true, TxRecord::Kind::MasterNodeReward, 3400000000ULL + rng.bounded(900000000), 0);
        // payments
        for (int i = 0; i < 9; ++i) {
            const quint64 h = start + 2880 * 8 + rng.bounded(quint32(std::max<quint64>(1, top - start - 2880 * 9)));
            const bool in = rng.bounded(3) == 0;
            mk(h, in, TxRecord::Kind::Transfer, (5 + rng.bounded(900)) * kAtomicPerBdx + rng.bounded(999999999), in ? 0 : 9000000 + rng.bounded(300000));
        }
        // a BNS purchase
        mk(start + 2880 * 20, false, TxRecord::Kind::Bns, 650 * kAtomicPerBdx, 27300000);
        BnsRecord b;
        b.name = QStringLiteral("demo.bdx");
        b.nameHash = QStringLiteral("o0Hq0fGvG7XgkN9X0p5mK3n0w9sRrzq7Y8b4cJm2sE4=");
        b.owner = m_primary;
        b.bchat = QStringLiteral("bd") + randomHex(rng, 32);
        b.wallet = m_primary;
        b.updateHeight = start + 2880 * 20;
        b.expirationHeight = b.updateHeight + 365 * 2880;
        m_bns.push_back(b);
        // one incoming in the mempool for realism
        MockTx p;
        p.rec.hash = randomHex(rng, 32);
        p.rec.incoming = true;
        p.rec.pending = true;
        p.rec.amount = 125 * kAtomicPerBdx;
        p.rec.blockHeight = top;
        p.rec.timestamp = QDateTime::currentDateTime().addSecs(-40);
        m_txs.push_back(p);
    }

    static QJsonObject txToJson(const MockTx &t)
    {
        const auto &r = t.rec;
        QJsonArray d;
        for (const auto &x : r.destinations)
            d << x;
        return {{"hash", r.hash}, {"kind", int(r.kind)}, {"amount", QString::number(r.amount)}, {"fee", QString::number(r.fee)},
                {"height", double(r.blockHeight)}, {"ts", double(r.timestamp.toSecsSinceEpoch())}, {"account", int(r.account)},
                {"dest", d}};
    }
    static MockTx txFromJson(const QJsonObject &o)
    {
        MockTx t;
        t.sentByUser = true;
        t.rec.hash = o.value("hash").toString();
        t.rec.kind = TxRecord::Kind(o.value("kind").toInt());
        t.rec.amount = o.value("amount").toString().toULongLong();
        t.rec.fee = o.value("fee").toString().toULongLong();
        t.rec.blockHeight = quint64(o.value("height").toDouble());
        t.rec.timestamp = QDateTime::fromSecsSinceEpoch(qint64(o.value("ts").toDouble()));
        t.rec.account = quint32(o.value("account").toInt());
        for (const auto &x : o.value("dest").toArray())
            t.rec.destinations << x.toString();
        return t;
    }

    QMutex m_mutex;
    QString m_path, m_password, m_error, m_seed, m_primary, m_daemon;
    NetType m_net = NetType::Mainnet;
    quint64 m_restoreHeight = 0;
    QRandomGenerator m_rng;
    QStringList m_keys;
    QStringList m_accountLabels;
    std::vector<Sub> m_subs;
    std::vector<AddressBookRecord> m_book;
    QHash<QString, QString> m_notes;
    QHash<QString, QString> m_attrs;
    std::vector<MockTx> m_txs;
    std::vector<StakeRecord> m_stakes;
    std::vector<BnsRecord> m_bns;
    quint64 m_lockedByStake = 0;
    QHash<QString, TokenInfo> m_tokens;                    // demo token registry
    QMap<std::pair<quint32, QString>, quint64> m_tokenBal;  // (account, token id) -> balance
    std::atomic<bool> m_connected{false}, m_refreshing{false};
    QElapsedTimer m_syncClock;
    IWalletListener *m_listener = nullptr;
};

// ---------------------------------------------------------------------------------------------
class MockEngine final : public IEngine {
public:
    QString name() const override { return QStringLiteral("Demo engine (simulated, no network)"); }
    bool isDemo() const override { return true; }
    QString errorString() const override { return m_error; }

    bool walletExists(const QString &path) override { return QFileInfo::exists(path + QStringLiteral(".keys")); }

    std::unique_ptr<IWallet> createWallet(const QString &path, const QString &pw, const QString &, NetType net) override
    {
        return make(path, pw, randomSeed(), net, networkHeightNow() - 10, false, fakeAddrFor(net));
    }
    std::unique_ptr<IWallet> openWallet(const QString &path, const QString &pw, NetType) override
    {
        const QJsonObject doc = MockWallet::readDoc(path);
        if (doc.isEmpty()) {
            m_error = QStringLiteral("Wallet file not found");
            return nullptr;
        }
        if (pwHash(pw, doc.value("salt").toString()) != doc.value("pw").toString().toLatin1()) {
            m_error = QStringLiteral("Invalid password");
            return nullptr;
        }
        m_error.clear();
        return std::make_unique<MockWallet>(path, pw, doc);
    }
    std::unique_ptr<IWallet> restoreFromSeed(const QString &path, const QString &pw, const QString &seed, NetType net,
                                             quint64 height, const QString &) override
    {
        const QStringList words = seed.simplified().split(' ', Qt::SkipEmptyParts);
        if (words.size() != 25) {
            m_error = QStringLiteral("Seed must be 25 words (got %1)").arg(words.size());
            return nullptr;
        }
        QRandomGenerator rng(qHash(words.join(' ')));
        return make(path, pw, words.join(' '), net, height, true, fakeAddress(rng, net));
    }
    std::unique_ptr<IWallet> restoreFromKeys(const QString &path, const QString &pw, NetType net, quint64 height,
                                             const QString &addr, const QString &view, const QString &) override
    {
        if (!addressValid(addr, net) || view.size() != 64) {
            m_error = QStringLiteral("Invalid address or view key");
            return nullptr;
        }
        return make(path, pw, randomSeed(), net, height, true, addr);
    }
    bool closeWallet(std::unique_ptr<IWallet> w, bool store) override
    {
        if (w && store)
            w->store();
        return true;
    }
    bool verifyPassword(const QString &keysFile, const QString &pw) override
    {
        QString p = keysFile;
        if (p.endsWith(QStringLiteral(".keys")))
            p.chop(5);
        const QJsonObject doc = MockWallet::readDoc(p);
        return pwHash(pw, doc.value("salt").toString()) == doc.value("pw").toString().toLatin1();
    }
    quint64 estimateHeightForDate(NetType net, const QDate &d) const override { return estimateHeightFromDate(net, d); }

    QString displayAmount(quint64 a) const override
    {
        QString s = QString::number(a / kAtomicPerBdx) + QLatin1Char('.') +
                    QStringLiteral("%1").arg(a % kAtomicPerBdx, kDecimals, 10, QLatin1Char('0'));
        while (s.endsWith('0') && !s.endsWith(QStringLiteral(".0")))
            s.chop(1);
        return s;
    }
    std::optional<quint64> amountFromString(const QString &in) const override
    {
        static const QRegularExpression re(QStringLiteral("^\\s*(\\d{0,10})(?:\\.(\\d{0,9}))?\\s*$"));
        const auto m = re.match(in);
        if (!m.hasMatch() || (m.captured(1).isEmpty() && m.captured(2).isEmpty()))
            return std::nullopt;
        const quint64 whole = m.captured(1).toULongLong();
        const QString frac = m.captured(2).leftJustified(kDecimals, QLatin1Char('0'));
        return whole * kAtomicPerBdx + frac.toULongLong();
    }
    bool addressValid(const QString &a, NetType net) const override
    {
        static const QRegularExpression b58(QStringLiteral("^[1-9A-HJ-NP-Za-km-z]+$"));
        return a.startsWith(prefixFor(net), Qt::CaseInsensitive) && a.size() >= 95 && a.size() <= 110 && b58.match(a).hasMatch();
    }
    bool masterNodeKeyValid(const QString &k) const override
    {
        static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{64}$"));
        return re.match(k).hasMatch();
    }

private:
    QString fakeAddrFor(NetType net)
    {
        auto rng = QRandomGenerator::securelySeeded();
        return fakeAddress(rng, net);
    }
    static QString randomSeed()
    {
        auto rng = QRandomGenerator::securelySeeded();
        QStringList w;
        for (int i = 0; i < 25; ++i)
            w << QString::fromLatin1(kWords[rng.bounded(int(std::size(kWords)))]);
        return w.join(' ');
    }
    std::unique_ptr<IWallet> make(const QString &path, const QString &pw, const QString &seed, NetType net,
                                  quint64 height, bool history, const QString &address)
    {
        if (walletExists(path)) {
            m_error = QStringLiteral("A wallet with this name already exists");
            return nullptr;
        }
        const QString salt = QStringLiteral("00");
        QJsonObject doc{{"engine", "beldex-gui demo"}, {"net", int(net)}, {"seed", seed}, {"address", address},
                        {"restoreHeight", double(height)}, {"demoHistory", history}, {"salt", salt},
                        {"pw", QString::fromLatin1(pwHash(pw, salt))}};
        if (!MockWallet::writeDoc(path, doc)) {
            m_error = QStringLiteral("Cannot write wallet file to %1").arg(QFileInfo(path).absolutePath());
            return nullptr;
        }
        m_error.clear();
        auto w = std::make_unique<MockWallet>(path, pw, doc);
        w->store();
        return w;
    }

    QString m_error;
};

} // namespace

std::unique_ptr<IEngine> createEngine() { return std::make_unique<MockEngine>(); }

} // namespace bdx
