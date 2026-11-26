// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
//
// WalletEngine is the seam between the Qt/QML layer and the wallet library.
//
//   QML  <->  libwalletqt (QObjects)  <->  bdx::IEngine / bdx::IWallet  <->  Beldex wallet_api
//
// Two implementations exist:
//   * BeldexEngine.cpp - links Beldex core's `wallet_api` (src/wallet/api/wallet2_api.h). Production.
//   * MockEngine.cpp   - in-memory demo wallet with simulated sync. Used for UI work and screenshots
//                        (cmake -DBELDEX_GUI_MOCK=ON). Never moves real funds.
//
// Everything here uses Qt value types so the rest of the app never includes Beldex headers.
// All IWallet methods may block (disk / network); libwalletqt calls the slow ones off the GUI thread.

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace bdx {

enum class NetType : int { Mainnet = 0, Testnet = 1, Devnet = 2 };

enum class ConnStatus : int { Disconnected = 0, Connected = 1, WrongVersion = 2 };

// Beldex priorities (wallet2.h): 0 default, 1 unimportant, ... 5 = flash.
enum Priority : uint32_t { PriorityDefault = 0, PriorityFlash = 5 };

struct TxRecord {
    enum class Kind { Transfer, MinerReward, MasterNodeReward, Stake, Bns,
                      TokenRegister, TokenMint, TokenBurn, TokenUpdate };
    QString hash;
    bool incoming = false;
    bool pending = false;
    bool failed = false;
    Kind kind = Kind::Transfer;
    quint64 amount = 0;
    quint64 fee = 0;
    quint64 blockHeight = 0;
    quint64 confirmations = 0;
    quint64 unlockTime = 0;
    QDateTime timestamp;
    QString paymentId;
    QString description;   // user note
    QString label;         // subaddress label
    quint32 account = 0;
    QList<quint32> subaddrIndices;
    QStringList destinations;  // outgoing only
    QString tokenId;           // privacy token moved (hex); empty = BDX. amount is then in token units.
};

// Privacy tokens (Beldex hard fork 22). Amounts are in the token's own atomic units (10^decimals).
struct TokenBalance {
    QString tokenId;
    QString ticker;
    quint64 balance = 0;
    quint64 unlocked = 0;
    int decimals = 0;
};

struct TokenInfo {
    QString tokenId;
    QString ticker;
    QString fullName;
    QString owner;        // owner's public spend key (hex)
    quint64 maxSupply = 0;
    quint64 supply = 0;
    int decimals = 0;
    QString metaInfo;
};

struct AccountRecord {
    quint32 index = 0;
    QString address;
    QString label;
    quint64 balance = 0;
    quint64 unlocked = 0;
};

struct SubaddressRecord {
    quint32 index = 0;
    QString address;
    QString label;
};

struct AddressBookRecord {
    int rowId = 0;
    QString address;
    QString description;
};

struct StakeRecord {
    QString masterNodeKey;
    quint64 amount = 0;
    std::optional<quint64> unlockHeight;
    bool awaiting = false;
    bool decommissioned = false;
};

struct BnsRecord {
    QString name;
    QString nameHash;
    QString owner;
    QString backupOwner;
    QString bchat;
    QString wallet;
    QString belnet;
    QString eth;
    quint64 updateHeight = 0;
    quint64 expirationHeight = 0;
};

struct BnsRequest {
    QString name;
    QString years;   // "1y" | "2y" | "5y" | "10y" (buy / renew)
    QString owner;   // empty = this wallet
    QString backupOwner;
    QString bchat;
    QString wallet;
    QString belnet;
    QString eth;
};

// A transaction that has been constructed but not yet broadcast.
class IPendingTx {
public:
    virtual ~IPendingTx() = default;
    virtual bool ok() const = 0;
    virtual QString errorString() const = 0;
    virtual quint64 amount() const = 0;
    virtual quint64 fee() const = 0;
    virtual quint64 txCount() const = 0;
    virtual QStringList txids() const = 0;
    virtual bool commit(bool flash) = 0;  // broadcast
};

struct StakeUnlockResult {
    bool success = false;
    QString message;
    std::unique_ptr<IPendingTx> tx;  // set when an unlock tx must be confirmed and committed
};

// Callbacks from the wallet's refresh thread. Implementations must be thread-safe.
class IWalletListener {
public:
    virtual ~IWalletListener() = default;
    virtual void moneySpent(const QString &txid, quint64 amount) = 0;
    virtual void moneyReceived(const QString &txid, quint64 amount) = 0;
    virtual void unconfirmedMoneyReceived(const QString &txid, quint64 amount) = 0;
    virtual void newBlock(quint64 height) = 0;
    virtual void updated() = 0;
    virtual void refreshed() = 0;
};

class IWallet {
public:
    virtual ~IWallet() = default;

    // status of the last operation
    virtual bool ok() const = 0;
    virtual QString errorString() const = 0;

    // identity & keys
    virtual QString seed() const = 0;
    virtual QString seedLanguage() const = 0;
    virtual QString address(quint32 account = 0, quint32 index = 0) const = 0;
    virtual QString path() const = 0;
    virtual NetType netType() const = 0;
    virtual QString secretViewKey() const = 0;
    virtual QString publicViewKey() const = 0;
    virtual QString secretSpendKey() const = 0;
    virtual QString publicSpendKey() const = 0;
    virtual bool watchOnly() const = 0;
    virtual bool setPassword(const QString &password) = 0;
    virtual bool store() = 0;

    // daemon & sync
    virtual bool init(const QString &daemonAddress, const QString &user, const QString &pass, bool trusted) = 0;
    virtual ConnStatus connectionStatus() const = 0;
    virtual void setRefreshFromHeight(quint64 height) = 0;
    // Core only remembers "restored from seed" in memory; without it a never-scanned wallet is treated as
    // brand new and fast-forwarded to the current block, skipping its history. Set before init().
    virtual void setRecovering(bool recovering) = 0;
    // Small key/value store saved inside the wallet cache file.
    virtual bool setAttribute(const QString &key, const QString &value) = 0;
    virtual QString attribute(const QString &key) const = 0;
    virtual quint64 refreshFromHeight() const = 0;
    virtual void startRefresh() = 0;
    virtual void pauseRefresh() = 0;
    virtual void refreshAsync() = 0;
    virtual void rescanAsync() = 0;
    virtual bool synchronized() const = 0;
    // True while the core refresh thread is scanning. Beldex core holds its node-RPC lock for the whole
    // scan, so calls that talk to the node (connectionStatus, daemonHeight, fees, stakes…) block until it ends.
    virtual bool isRefreshing() = 0;
    // Interrupts a running scan after its current batch (lock-free; needs scripts/patches/0001-*). Progress so
    // far is kept; startRefresh() continues from there.
    virtual void stopScan() = 0;
    virtual quint64 blockHeight() const = 0;
    virtual quint64 daemonHeight() const = 0;
    virtual quint64 daemonTargetHeight() const = 0;
    virtual std::optional<int> hardForkVersion() const = 0;
    virtual void setListener(IWalletListener *listener) = 0;

    // balances & accounts
    virtual quint64 balance(quint32 account) const = 0;
    virtual quint64 unlockedBalance(quint32 account) const = 0;
    virtual std::vector<AccountRecord> accounts() = 0;
    virtual void addAccount(const QString &label) = 0;
    virtual void setAccountLabel(quint32 account, const QString &label) = 0;
    virtual std::vector<SubaddressRecord> subaddresses(quint32 account) = 0;
    virtual void addSubaddress(quint32 account, const QString &label) = 0;
    virtual void setSubaddressLabel(quint32 account, quint32 index, const QString &label) = 0;

    // history & address book
    virtual std::vector<TxRecord> history() = 0;
    virtual bool setTxNote(const QString &txid, const QString &note) = 0;
    virtual QString txKey(const QString &txid) const = 0;
    virtual std::vector<AddressBookRecord> addressBook() = 0;
    virtual bool addAddressBookRow(const QString &address, const QString &description) = 0;
    virtual bool deleteAddressBookRow(int rowId) = 0;

    // sending
    // tokenId empty = BDX, else every destination receives that privacy token.
    virtual std::unique_ptr<IPendingTx> createTransaction(const QStringList &destinations,
                                                          const QList<quint64> &amounts,
                                                          uint32_t priority, quint32 account,
                                                          const QString &tokenId = {}) = 0;
    virtual std::unique_ptr<IPendingTx> createSweepAll(uint32_t priority, quint32 account) = 0;  // BDX to ourselves
    // Sweep to `address` (empty = this account): what is a token id, "bdx" (native only) or "all" (BDX + every token).
    virtual std::unique_ptr<IPendingTx> sweep(const QString &what, const QString &address, uint32_t priority,
                                              quint32 account) = 0;
    virtual quint64 estimateFee(uint32_t priority, quint32 recipients) const = 0;

    // master node staking
    virtual std::vector<StakeRecord> stakes() = 0;
    virtual std::unique_ptr<IPendingTx> stake(const QString &masterNodeKey, quint64 amount) = 0;
    virtual StakeUnlockResult canRequestStakeUnlock(const QString &masterNodeKey) = 0;
    virtual StakeUnlockResult requestStakeUnlock(const QString &masterNodeKey) = 0;

    // Beldex Name Service
    virtual std::vector<BnsRecord> bnsRecords() = 0;
    virtual std::unique_ptr<IPendingTx> buyBns(const BnsRequest &req, quint32 account) = 0;
    virtual std::unique_ptr<IPendingTx> renewBns(const QString &name, const QString &years, quint32 account) = 0;
    virtual std::unique_ptr<IPendingTx> updateBns(const BnsRequest &req, quint32 account) = 0;

    // privacy tokens
    virtual std::vector<TokenBalance> tokenBalances(quint32 account) = 0;
    // The token registry when the engine has one without a node (demo engine). The Beldex engine returns
    // nothing: the GUI reads the registry from the node itself (TokenRegistry), in parallel.
    virtual std::vector<TokenInfo> knownTokens() = 0;
    // descriptorJson: core's token descriptor (ticker, full_name, total_max_supply, current_supply, decimal_point,
    // meta_info). The new token's id is written to *tokenId.
    virtual std::unique_ptr<IPendingTx> registerToken(const QString &descriptorJson, QString *tokenId,
                                                      uint32_t priority, quint32 account) = 0;
    virtual std::unique_ptr<IPendingTx> mintToken(const QString &tokenId, quint64 amount, uint32_t priority,
                                                  quint32 account) = 0;
    virtual std::unique_ptr<IPendingTx> burnToken(const QString &tokenId, quint64 amount, uint32_t priority,
                                                  quint32 account) = 0;
    // descriptorJson: {"meta_info": "...", "owner": "<address>"} (owner optional). Primary account only.
    virtual std::unique_ptr<IPendingTx> updateToken(const QString &tokenId, const QString &descriptorJson,
                                                    uint32_t priority) = 0;

    // tools
    virtual QString signMessage(const QString &message) = 0;
    virtual bool verifySignedMessage(const QString &message, const QString &address, const QString &signature) const = 0;
};

class IEngine {
public:
    virtual ~IEngine() = default;
    virtual QString name() const = 0;     // shown in About
    virtual bool isDemo() const = 0;      // true for MockEngine: UI shows a banner
    virtual QString errorString() const = 0;
    virtual QString logPath() const { return {}; }  // wallet library log file, if any

    virtual bool walletExists(const QString &path) = 0;
    virtual std::unique_ptr<IWallet> createWallet(const QString &path, const QString &password,
                                                  const QString &language, NetType net) = 0;
    virtual std::unique_ptr<IWallet> openWallet(const QString &path, const QString &password, NetType net) = 0;
    virtual std::unique_ptr<IWallet> restoreFromSeed(const QString &path, const QString &password,
                                                     const QString &seed, NetType net,
                                                     quint64 restoreHeight, const QString &seedOffset) = 0;
    virtual std::unique_ptr<IWallet> restoreFromKeys(const QString &path, const QString &password,
                                                     NetType net, quint64 restoreHeight,
                                                     const QString &address, const QString &viewKey,
                                                     const QString &spendKey) = 0;
    virtual bool closeWallet(std::unique_ptr<IWallet> wallet, bool store) = 0;
    virtual bool verifyPassword(const QString &keysFile, const QString &password) = 0;
    virtual quint64 estimateHeightForDate(NetType net, const QDate &date) const = 0;

    // pure helpers (no wallet needed)
    virtual QString displayAmount(quint64 atomic) const = 0;
    virtual std::optional<quint64> amountFromString(const QString &amount) const = 0;
    virtual bool addressValid(const QString &address, NetType net) const = 0;
    virtual bool masterNodeKeyValid(const QString &key) const = 0;
};

// Defined by whichever engine is compiled in.
std::unique_ptr<IEngine> createEngine();

constexpr int kDecimals = 9;                 // 1 BDX = 10^9 atomic units
constexpr quint64 kAtomicPerBdx = 1000000000ULL;

} // namespace bdx
