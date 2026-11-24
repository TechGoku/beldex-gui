// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "WalletEngine.h"

#include <QDate>
#include <QDateTime>
#include <QTimeZone>

namespace bdx {

// Rough "restore height from date" estimate. Anchored on a known mainnet block and
// Beldex's 30 s block time, minus a one-week safety margin so a restore never starts
// after the wallet's first transaction. Testnet/devnet return 0 (scan everything).
inline quint64 estimateHeightFromDate(NetType net, const QDate &date)
{
    if (net != NetType::Mainnet || !date.isValid())
        return 0;
    constexpr quint64 kAnchorHeight = 5953938;                 // explorer.beldex.io, 2026-11-24
    const QDateTime anchorTime(QDate(2026, 11, 24), QTime(10, 54), QTimeZone::UTC);
    constexpr qint64 kBlockSeconds = 30;
    constexpr qint64 kMarginBlocks = 7 * 24 * 3600 / kBlockSeconds;

    const qint64 secs = QDateTime(date, QTime(0, 0), QTimeZone::UTC).secsTo(anchorTime);
    const qint64 h = qint64(kAnchorHeight) - secs / kBlockSeconds - kMarginBlocks;
    return h > 0 ? quint64(h) : 0;
}

} // namespace bdx
