// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#include "TransactionHistoryModel.h"

#include "libwalletqt/Amount.h"

using bdx::TxRecord;

TransactionHistoryModel::TransactionHistoryModel(QObject *parent) : QAbstractListModel(parent) {}

int TransactionHistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_view.size());
}

QString TransactionHistoryModel::kindName(const TxRecord &t)
{
    switch (t.kind) {
    case TxRecord::Kind::MinerReward: return QStringLiteral("miner");
    case TxRecord::Kind::MasterNodeReward: return QStringLiteral("reward");
    case TxRecord::Kind::Stake: return QStringLiteral("stake");
    case TxRecord::Kind::Bns: return QStringLiteral("bns");
    default: return t.incoming ? QStringLiteral("in") : QStringLiteral("out");
    }
}

QVariantMap TransactionHistoryModel::toMap(const TxRecord &t)
{
    return {
        {"hash", t.hash},
        {"incoming", t.incoming},
        {"pending", t.pending},
        {"failed", t.failed},
        {"kind", kindName(t)},
        {"amount", bdx::formatAtomic(t.amount, 4)},
        {"amountFull", bdx::formatAtomic(t.amount, 9)},
        {"fee", bdx::formatAtomic(t.fee, 9)},
        {"height", double(t.blockHeight)},
        {"confirmations", double(t.confirmations)},
        {"timestamp", t.timestamp},
        {"dateText", t.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))},
        {"paymentId", t.paymentId},
        {"note", t.description},
        {"destinations", t.destinations},
        {"account", t.account},
        {"unlockTime", double(t.unlockTime)},
    };
}

QVariant TransactionHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_view.size()))
        return {};
    const TxRecord &t = m_all[size_t(m_view[size_t(index.row())])];
    switch (role) {
    case HashRole: return t.hash;
    case IncomingRole: return t.incoming;
    case PendingRole: return t.pending;
    case FailedRole: return t.failed;
    case KindRole: return kindName(t);
    case AmountRole: return bdx::formatAtomic(t.amount, 4);
    case FeeRole: return bdx::formatAtomic(t.fee, 9);
    case HeightRole: return double(t.blockHeight);
    case ConfirmationsRole: return double(t.confirmations);
    case TimestampRole: return t.timestamp;
    case DateTextRole: return t.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    case PaymentIdRole: return t.paymentId;
    case NoteRole: return t.description;
    case DestinationsRole: return t.destinations;
    case AccountRole: return t.account;
    case UnlockTimeRole: return double(t.unlockTime);
    }
    return {};
}

QHash<int, QByteArray> TransactionHistoryModel::roleNames() const
{
    return {
        {HashRole, "hash"}, {IncomingRole, "incoming"}, {PendingRole, "pending"}, {FailedRole, "failed"},
        {KindRole, "kind"}, {AmountRole, "amount"}, {FeeRole, "fee"}, {HeightRole, "height"},
        {ConfirmationsRole, "confirmations"}, {TimestampRole, "timestamp"}, {DateTextRole, "dateText"},
        {PaymentIdRole, "paymentId"}, {NoteRole, "note"}, {DestinationsRole, "destinations"},
        {AccountRole, "account"}, {UnlockTimeRole, "unlockTime"},
    };
}

void TransactionHistoryModel::setTransactions(std::vector<TxRecord> txs)
{
    beginResetModel();
    m_all = std::move(txs);
    std::stable_sort(m_all.begin(), m_all.end(), [](const TxRecord &a, const TxRecord &b) {
        if (a.pending != b.pending)
            return a.pending;  // mempool first
        return a.timestamp > b.timestamp;
    });
    m_view.clear();
    for (size_t i = 0; i < m_all.size(); ++i)
        if (matches(m_all[i]))
            m_view.push_back(int(i));
    endResetModel();
    emit countChanged();
}

void TransactionHistoryModel::setFilter(const QString &f)
{
    if (f == m_filter)
        return;
    m_filter = f;
    emit filterChanged();
    rebuild();
}

void TransactionHistoryModel::setSearch(const QString &s)
{
    if (s == m_search)
        return;
    m_search = s;
    emit searchChanged();
    rebuild();
}

bool TransactionHistoryModel::matches(const TxRecord &t) const
{
    const QString k = kindName(t);
    if (m_filter == QLatin1String("in") && !t.incoming) return false;
    if (m_filter == QLatin1String("out") && t.incoming) return false;
    if (m_filter == QLatin1String("reward") && k != QLatin1String("reward") && k != QLatin1String("miner")) return false;
    if (m_filter == QLatin1String("stake") && k != QLatin1String("stake")) return false;
    if (m_filter == QLatin1String("bns") && k != QLatin1String("bns")) return false;
    if (m_filter == QLatin1String("pending") && !t.pending) return false;
    const QString s = m_search.trimmed();
    if (s.isEmpty())
        return true;
    if (t.hash.contains(s, Qt::CaseInsensitive) || t.description.contains(s, Qt::CaseInsensitive) ||
        t.paymentId.contains(s, Qt::CaseInsensitive) || QString::number(t.blockHeight) == s)
        return true;
    for (const auto &d : t.destinations)
        if (d.contains(s, Qt::CaseInsensitive))
            return true;
    return false;
}

void TransactionHistoryModel::rebuild()
{
    beginResetModel();
    m_view.clear();
    for (size_t i = 0; i < m_all.size(); ++i)
        if (matches(m_all[i]))
            m_view.push_back(int(i));
    endResetModel();
    emit countChanged();
}

QVariantMap TransactionHistoryModel::get(int row) const
{
    if (row < 0 || row >= int(m_view.size()))
        return {};
    return toMap(m_all[size_t(m_view[size_t(row)])]);
}

QString TransactionHistoryModel::exportCsv() const
{
    QString out = QStringLiteral("date,height,direction,type,amount_bdx,fee_bdx,txid,payment_id,note\n");
    auto esc = [](QString v) {
        v.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + v + QLatin1Char('"');
    };
    for (const auto &t : m_all)
        out += QStringList{t.timestamp.toUTC().toString(Qt::ISODate), QString::number(t.blockHeight),
                           t.incoming ? QStringLiteral("in") : QStringLiteral("out"), kindName(t),
                           bdx::formatAtomic(t.amount, 9, false), bdx::formatAtomic(t.fee, 9, false), t.hash,
                           t.paymentId, esc(t.description)}
                   .join(QLatin1Char(',')) +
               QLatin1Char('\n');
    return out;
}
