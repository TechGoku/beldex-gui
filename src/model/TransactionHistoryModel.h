// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "engine/WalletEngine.h"

#include <QAbstractListModel>
#include <QQmlEngine>

#include <vector>

class TransactionHistoryModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by Wallet")

    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)   // all | in | out | reward | stake | bns | pending
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)   // matches hash, note, address, payment id
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)

public:
    enum Roles {
        HashRole = Qt::UserRole + 1,
        IncomingRole,
        PendingRole,
        FailedRole,
        KindRole,
        AmountRole,
        FeeRole,
        HeightRole,
        ConfirmationsRole,
        TimestampRole,
        DateTextRole,
        PaymentIdRole,
        NoteRole,
        DestinationsRole,
        AccountRole,
        UnlockTimeRole,
    };

    explicit TransactionHistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setTransactions(std::vector<bdx::TxRecord> txs);
    const std::vector<bdx::TxRecord> &all() const { return m_all; }

    QString filter() const { return m_filter; }
    void setFilter(const QString &f);
    QString search() const { return m_search; }
    void setSearch(const QString &s);
    int totalCount() const { return int(m_all.size()); }

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QString exportCsv() const;

    static QString kindName(const bdx::TxRecord &t);
    static QVariantMap toMap(const bdx::TxRecord &t);

signals:
    void filterChanged();
    void searchChanged();
    void countChanged();

private:
    void rebuild();
    bool matches(const bdx::TxRecord &t) const;

    std::vector<bdx::TxRecord> m_all;
    std::vector<int> m_view;  // indices into m_all
    QString m_filter = QStringLiteral("all");
    QString m_search;
};
