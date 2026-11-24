// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "engine/WalletEngine.h"

#include <QLocale>
#include <QString>

namespace bdx {

// 1234567890123 -> "1,234.5678" (maxDecimals = 4, trailing zeros trimmed, at least 2 kept)
inline QString formatAtomic(quint64 atomic, int maxDecimals = 4, bool group = true)
{
    const quint64 whole = atomic / kAtomicPerBdx;
    QString frac = QStringLiteral("%1").arg(atomic % kAtomicPerBdx, kDecimals, 10, QLatin1Char('0')).left(maxDecimals);
    while (frac.size() > 2 && frac.endsWith(QLatin1Char('0')))
        frac.chop(1);
    const QString w = group ? QLocale(QLocale::English).toString(whole) : QString::number(whole);
    return frac.isEmpty() ? w : w + QLatin1Char('.') + frac;
}

} // namespace bdx
