// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#include "TokenRegistry.h"

#include "Amount.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

using bdx::TokenInfo;

namespace {
constexpr int kParallel = 6;   // Qt's per-host connection limit

TokenInfo fromJson(const QJsonObject &o)
{
    TokenInfo t;
    t.tokenId = o.value("token_id").toString();
    t.ticker = o.value("ticker").toString();
    t.fullName = o.value("full_name").toString();
    t.owner = o.value("owner").toString();
    // uint64 supplies can exceed 2^53, so take them from the JSON text, not via double.
    t.maxSupply = o.value("total_max_supply").toVariant().toULongLong();
    t.supply = o.value("current_supply").toVariant().toULongLong();
    t.decimals = o.value("decimal_point").toInt();
    t.metaInfo = o.value("meta_info").toString();
    return t;
}

QJsonObject toJson(const TokenInfo &t)
{
    return {{"token_id", t.tokenId}, {"ticker", t.ticker}, {"full_name", t.fullName}, {"owner", t.owner},
            {"total_max_supply", QString::number(t.maxSupply)}, {"current_supply", QString::number(t.supply)},
            {"decimal_point", t.decimals}, {"meta_info", t.metaInfo}};
}
} // namespace

TokenRegistry::TokenRegistry(int netType, QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)), m_netType(netType)
{
}

void TokenRegistry::setNode(const QString &address)
{
    if (address == m_node)
        return;
    m_node = address;
    if (m_loading) {  // answers from the old node are dropped
        ++m_generation;
        m_loading = false;
        m_queue.clear();
        m_inFlight = 0;
        emit stateChanged();
    }
}

void TokenRegistry::setAll(const std::vector<TokenInfo> &tokens)
{
    m_tokens.clear();
    for (const auto &t : tokens)
        m_tokens.insert(t.tokenId, t);
    m_updated = QDateTime::currentDateTime();
    emit changed();
}

QString TokenRegistry::updated() const
{
    return m_updated.isValid() ? m_updated.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
}

std::optional<TokenInfo> TokenRegistry::info(const QString &tokenId) const
{
    const auto it = m_tokens.constFind(tokenId);
    if (it == m_tokens.cend())
        return std::nullopt;
    return *it;
}

std::vector<TokenInfo> TokenRegistry::byOwner(const QString &spendKey) const
{
    std::vector<TokenInfo> out;
    for (const auto &t : m_tokens)
        if (!spendKey.isEmpty() && t.owner.compare(spendKey, Qt::CaseInsensitive) == 0)
            out.push_back(t);
    std::sort(out.begin(), out.end(), [](const TokenInfo &a, const TokenInfo &b) { return a.ticker < b.ticker; });
    return out;
}

QVariantMap TokenRegistry::toMap(const TokenInfo &t)
{
    return {{"tokenId", t.tokenId},
            {"ticker", t.ticker},
            {"fullName", t.fullName},
            {"owner", t.owner},
            {"decimals", t.decimals},
            {"supply", bdx::formatUnits(t.supply, t.decimals, t.decimals)},
            {"maxSupply", bdx::formatUnits(t.maxSupply, t.decimals, t.decimals)},
            {"mintable", bdx::formatUnits(t.maxSupply - std::min(t.supply, t.maxSupply), t.decimals, t.decimals, false)},
            {"metaInfo", t.metaInfo}};
}

QVariantList TokenRegistry::search(const QString &text, const QString &ownerKey, int limit) const
{
    const QString q = text.trimmed();
    std::vector<const TokenInfo *> hits;
    for (const auto &t : m_tokens) {
        if (!ownerKey.isEmpty() && t.owner.compare(ownerKey, Qt::CaseInsensitive) != 0)
            continue;
        if (!q.isEmpty() && !t.ticker.contains(q, Qt::CaseInsensitive) && !t.fullName.contains(q, Qt::CaseInsensitive) &&
            !t.tokenId.startsWith(q, Qt::CaseInsensitive))
            continue;
        hits.push_back(&t);
    }
    std::sort(hits.begin(), hits.end(), [](const TokenInfo *a, const TokenInfo *b) {
        const int c = a->ticker.compare(b->ticker, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a->tokenId < b->tokenId;
    });
    QVariantList out;
    for (const auto *t : hits) {
        if (out.size() >= limit)
            break;
        out << toMap(*t);
    }
    return out;
}

QVariantMap TokenRegistry::get(const QString &tokenId) const
{
    const auto t = info(tokenId);
    return t ? toMap(*t) : QVariantMap{};
}

void TokenRegistry::rpc(const QString &method, const QJsonObject &params, std::function<void(QJsonObject, QString)> done)
{
    const QString base = m_node.contains(QLatin1String("://")) ? m_node : QStringLiteral("http://") + m_node;
    QNetworkRequest req(QUrl(base + QStringLiteral("/json_rpc")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(20000);
    const QByteArray body = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", "0"}, {"method", method}, {"params", params}})
                                .toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_net->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, reply->errorString());
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.contains("error")) {
            done({}, o.value("error").toObject().value("message").toString());
            return;
        }
        done(o.value("result").toObject(), {});
    });
}

void TokenRegistry::refresh()
{
    if (m_loading || m_offline || m_node.isEmpty())
        return;
    m_loading = true;
    m_error.clear();
    m_done = m_total = m_failed = 0;
    const int gen = ++m_generation;
    emit stateChanged();
    rpc(QStringLiteral("get_token_list"), {{"offset", 0}, {"count", 1000000}}, [this, gen](QJsonObject r, QString err) {
        if (gen != m_generation)
            return;
        if (!err.isEmpty() || !r.value("token_ids").isArray()) {
            m_loading = false;
            m_error = err.isEmpty() ? tr("The node did not return a token list") : err;
            emit stateChanged();
            return;
        }
        QStringList ids;
        for (const auto &v : r.value("token_ids").toArray())
            ids << v.toString();
        // Tokens that left the list (never happens today) are dropped; known ones refresh in place.
        for (auto it = m_tokens.begin(); it != m_tokens.end();)
            it = ids.contains(it.key()) ? std::next(it) : m_tokens.erase(it);
        m_queue = ids;
        m_total = int(ids.size());
        emit stateChanged();
        pump();
    });
}

void TokenRegistry::pump()
{
    const int gen = m_generation;
    while (m_inFlight < kParallel && !m_queue.isEmpty()) {
        const QString id = m_queue.takeFirst();
        ++m_inFlight;
        rpc(QStringLiteral("get_token_info"), {{"token_id", id}}, [this, gen, id](QJsonObject r, QString err) {
            if (gen != m_generation)
                return;
            --m_inFlight;
            ++m_done;
            if (err.isEmpty() && r.contains("token_id"))
                m_tokens.insert(id, fromJson(r));
            else
                ++m_failed;
            if (m_done % 60 == 0)
                emit changed();
            emit stateChanged();
            if (m_queue.isEmpty() && m_inFlight == 0) {
                m_loading = false;
                if (m_failed)
                    m_error = tr("%n token(s) could not be read; refresh to try again", nullptr, m_failed);
                m_updated = QDateTime::currentDateTime();
                save();
                emit changed();
                emit stateChanged();
            } else {
                pump();
            }
        });
    }
}

void TokenRegistry::refreshToken(const QString &tokenId)
{
    if (m_offline || m_node.isEmpty() || tokenId.isEmpty())
        return;
    rpc(QStringLiteral("get_token_info"), {{"token_id", tokenId}}, [this, tokenId](QJsonObject r, QString err) {
        if (!err.isEmpty() || !r.contains("token_id"))
            return;
        m_tokens.insert(tokenId, fromJson(r));
        save();
        emit changed();
        emit tokenUpdated(tokenId);
    });
}

QString TokenRegistry::cachePath() const
{
    const char *net = m_netType == 1 ? "testnet" : m_netType == 2 ? "devnet" : "mainnet";
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("token-registry-%1.json").arg(QLatin1String(net)));
}

void TokenRegistry::loadCache()
{
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    for (const auto &v : doc.value("tokens").toArray()) {
        const TokenInfo t = fromJson(v.toObject());
        if (!t.tokenId.isEmpty())
            m_tokens.insert(t.tokenId, t);
    }
    m_updated = QDateTime::fromString(doc.value("updated").toString(), Qt::ISODate);
}

void TokenRegistry::save() const
{
    if (m_offline)
        return;
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QJsonArray arr;
    for (const auto &t : m_tokens)
        arr << toJson(t);
    QSaveFile f(cachePath());
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(QJsonObject{{"updated", m_updated.toString(Qt::ISODate)}, {"tokens", arr}}).toJson(QJsonDocument::Compact));
    f.commit();
}
