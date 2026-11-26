// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#include "PendingTransaction.h"

#include "Amount.h"

PendingTransaction::PendingTransaction(std::unique_ptr<bdx::IPendingTx> tx, QString kind, QStringList destinations,
                                       bool flash, QString note, QObject *parent)
    : QObject(parent), m_tx(std::move(tx)), m_kind(std::move(kind)), m_destinations(std::move(destinations)),
      m_flash(flash), m_note(std::move(note))
{
}

PendingTransaction::~PendingTransaction() = default;

QString PendingTransaction::amount() const
{
    return m_unit == QLatin1String("BDX") ? bdx::formatAtomic(m_tx ? m_tx->amount() : 0, 9) : m_amountText;
}
QString PendingTransaction::fee() const { return bdx::formatAtomic(m_tx ? m_tx->fee() : 0, 9); }
QString PendingTransaction::total() const
{
    return bdx::formatAtomic(m_tx ? m_tx->amount() + m_tx->fee() : 0, 9);
}
