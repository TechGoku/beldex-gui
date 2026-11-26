// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "engine/WalletEngine.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <optional>
#include <vector>

class QNetworkAccessManager;

// Every privacy token registered on the chain (Beldex hard fork 22), read from the node's
// get_token_list / get_token_info RPCs. Core's TokensByOwner asks about each token one after another
// (minutes on testnet, while blocking the wallet); this loads them several at a time and keeps a copy on
// disk, so later opens list tokens at once and refresh in the background.
class TokenRegistry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by Wallet")

    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(int loaded READ loaded NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QString updated READ updated NOTIFY changed)   // "2026-11-26 10:15" or "" if never loaded

public:
    TokenRegistry(int netType, QObject *parent = nullptr);

    void setNode(const QString &address);
    // Demo engine: the registry comes from the engine instead of a node.
    void setAll(const std::vector<bdx::TokenInfo> &tokens);
    void setOffline(bool offline) { m_offline = offline; }
    void loadCache();  // the copy saved by the last refresh

    bool loading() const { return m_loading; }
    int loaded() const { return m_done; }
    int total() const { return m_total; }
    QString error() const { return m_error; }
    int count() const { return int(m_tokens.size()); }
    QString updated() const;

    std::optional<bdx::TokenInfo> info(const QString &tokenId) const;
    std::vector<bdx::TokenInfo> byOwner(const QString &spendKey) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshToken(const QString &tokenId);
    // text matches ticker, name or id; ownerKey (hex spend key) narrows to one owner. Newest ids are unordered,
    // so results are sorted by ticker.
    Q_INVOKABLE QVariantList search(const QString &text, const QString &ownerKey = {}, int limit = 200) const;
    Q_INVOKABLE QVariantMap get(const QString &tokenId) const;

    static QVariantMap toMap(const bdx::TokenInfo &t);

signals:
    void changed();
    void stateChanged();
    void tokenUpdated(const QString &tokenId);

private:
    void rpc(const QString &method, const QJsonObject &params, std::function<void(QJsonObject, QString)> done);
    void pump();
    void save() const;
    QString cachePath() const;

    QNetworkAccessManager *m_net;
    int m_netType;
    QString m_node;
    bool m_offline = false;
    QHash<QString, bdx::TokenInfo> m_tokens;
    QDateTime m_updated;

    // refresh in progress
    bool m_loading = false;
    int m_generation = 0;
    QStringList m_queue;
    int m_inFlight = 0, m_done = 0, m_total = 0, m_failed = 0;
    QString m_error;
};
