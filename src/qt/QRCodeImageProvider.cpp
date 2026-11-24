// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#include "QRCodeImageProvider.h"

#include "qrcodegen.hpp"

#include <QPainter>
#include <QUrl>

QImage QRCodeImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString text = QUrl::fromPercentEncoding(id.toUtf8());
    const auto qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int quiet = 2;
    const int modules = qr.getSize() + quiet * 2;
    const int side = requestedSize.isValid() ? std::max(requestedSize.width(), requestedSize.height()) : 512;
    const int scale = std::max(1, side / modules);

    QImage img(modules * scale, modules * scale, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    for (int y = 0; y < qr.getSize(); ++y)
        for (int x = 0; x < qr.getSize(); ++x)
            if (qr.getModule(x, y))
                p.drawRect((x + quiet) * scale, (y + quiet) * scale, scale, scale);
    p.end();
    if (size)
        *size = img.size();
    return img;
}
