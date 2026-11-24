// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#include "daemon/DaemonManager.h"
#include "libwalletqt/WalletManager.h"
#include "qt/QRCodeImageProvider.h"
#include "ScreenshotTour.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Beldex"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("beldex.io"));
    QGuiApplication::setApplicationName(QStringLiteral("beldex-gui"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Beldex Wallet"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.beldex.WalletGui"));  // Wayland/X11 icon + dock grouping
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/BeldexGui/images/logo-icon.png")));

    for (const char *f : {":/qt/qml/BeldexGui/fonts/Michroma-Regular.ttf", ":/qt/qml/BeldexGui/fonts/SpaceMono-Regular.ttf",
                          ":/qt/qml/BeldexGui/fonts/SpaceMono-Bold.ttf"})
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(f)) < 0)
            qWarning("Failed to load font %s", f);

    // Basic is the most customisable built-in style; every control is restyled in qml/components.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Create the singleton before QML so it outlives the engine's object tree.
    WalletManager manager(nullptr);
    DaemonManager daemonManager(nullptr);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("qrcode"), new QRCodeImageProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("BeldexGui", "Main");

    std::unique_ptr<ScreenshotTour> tour;
    if (const QString dir = qEnvironmentVariable("BELDEX_GUI_SHOTS"); !dir.isEmpty()) {
        tour = std::make_unique<ScreenshotTour>(engine, manager, dir);
        tour->start();
    }
    const int rc = app.exec();
    manager.closeWallet();          // normally already done by Main.qml's shutdown screen
    manager.waitForClose();
    daemonManager.shutdown(30000);  // stop a local node we started (it saves its chain on SIGTERM)
    return rc;
}
