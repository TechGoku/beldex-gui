// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QQuickImageProvider>

// image://qrcode/<percent-encoded text>  ->  QR code image (black on white, quiet zone included)
class QRCodeImageProvider : public QQuickImageProvider {
public:
    QRCodeImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
