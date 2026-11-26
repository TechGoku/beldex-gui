// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "engine/WalletEngine.h"

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

#include <memory>

// A constructed-but-unsent transaction, shown in the confirm dialog. Commit with
// Wallet.commitTransaction(tx); the object is deleted by the Wallet afterwards.
class PendingTransaction : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by Wallet")

    Q_PROPERTY(bool ok READ ok CONSTANT)
    Q_PROPERTY(QString errorString READ errorString CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)              // transfer | sweep | stake | unlock | bns-buy | bns-renew | bns-update
                                                             // | token-register | token-mint | token-burn | token-update
    Q_PROPERTY(QString amount READ amount CONSTANT)          // formatted, in `unit`
    Q_PROPERTY(QString unit READ unit CONSTANT)              // "BDX", a token ticker, or "" (sweep of BDX and all tokens)
    Q_PROPERTY(QString tokenId READ tokenId CONSTANT)
    Q_PROPERTY(QString fee READ fee CONSTANT)                // formatted BDX
    Q_PROPERTY(QString total READ total CONSTANT)
    Q_PROPERTY(int txCount READ txCount CONSTANT)
    Q_PROPERTY(QStringList destinations READ destinations CONSTANT)
    Q_PROPERTY(QString note READ note CONSTANT)              // extra human-readable context for the dialog
    Q_PROPERTY(bool flash READ flash CONSTANT)

public:
    PendingTransaction(std::unique_ptr<bdx::IPendingTx> tx, QString kind, QStringList destinations, bool flash,
                       QString note, QObject *parent = nullptr);
    ~PendingTransaction() override;

    bool ok() const { return m_tx && m_tx->ok(); }
    QString errorString() const { return m_tx ? m_tx->errorString() : m_note; }
    QString kind() const { return m_kind; }
    QString amount() const;
    QString fee() const;
    QString total() const;
    int txCount() const { return m_tx ? int(m_tx->txCount()) : 0; }
    QStringList destinations() const { return m_destinations; }
    QString note() const { return m_note; }
    bool flash() const { return m_flash; }
    QString unit() const { return m_unit; }
    QString tokenId() const { return m_tokenId; }
    // Token transactions: the amount is in token units and the fee in BDX, so the dialog shows them apart.
    void setToken(const QString &tokenId, const QString &unit, const QString &amountText)
    {
        m_tokenId = tokenId;
        m_unit = unit;
        m_amountText = amountText;
    }

    bdx::IPendingTx *raw() const { return m_tx.get(); }

private:
    std::unique_ptr<bdx::IPendingTx> m_tx;
    QString m_kind;
    QStringList m_destinations;
    bool m_flash;
    QString m_note;
    QString m_unit = QStringLiteral("BDX");
    QString m_tokenId, m_amountText;
};
