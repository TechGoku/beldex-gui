// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "engine/WalletEngine.h"

#include <QLocale>
#include <QRegularExpression>
#include <QString>

#include <algorithm>
#include <limits>
#include <optional>

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

// Privacy tokens choose their own decimals (0..18): 1234500 with 4 decimals -> "123.45".
inline QString formatUnits(quint64 atomic, int decimals, int maxDecimals = 4, bool group = true)
{
    if (decimals <= 0)
        return group ? QLocale(QLocale::English).toString(atomic) : QString::number(atomic);
    quint64 div = 1;
    for (int i = 0; i < decimals; ++i)
        div *= 10;
    const quint64 whole = atomic / div;
    QString frac = QStringLiteral("%1").arg(atomic % div, decimals, 10, QLatin1Char('0')).left(maxDecimals);
    while (frac.size() > std::min(2, decimals) && frac.endsWith(QLatin1Char('0')))
        frac.chop(1);
    const QString w = group ? QLocale(QLocale::English).toString(whole) : QString::number(whole);
    return frac.isEmpty() ? w : w + QLatin1Char('.') + frac;
}

// "123.45" with 4 decimals -> 1234500. nullopt if malformed, too precise or above 2^64-1.
inline std::optional<quint64> parseUnits(const QString &text, int decimals)
{
    static const QRegularExpression re(QStringLiteral("^(\\d*)(?:\\.(\\d*))?$"));
    const QString t = QString(text).remove(QLatin1Char(',')).trimmed();
    const auto m = re.match(t);
    if (t.isEmpty() || t == QLatin1String(".") || !m.hasMatch())
        return std::nullopt;
    QString frac = m.captured(2);
    while (frac.size() > decimals && frac.endsWith(QLatin1Char('0')))
        frac.chop(1);
    if (frac.size() > decimals)
        return std::nullopt;
    const QString digits = m.captured(1) + frac.leftJustified(decimals, QLatin1Char('0'));
    quint64 v = 0;
    for (const QChar c : digits) {
        const quint64 d = quint64(c.unicode() - '0');
        if (v > (std::numeric_limits<quint64>::max() - d) / 10)
            return std::nullopt;
        v = v * 10 + d;
    }
    return v;
}

} // namespace bdx
