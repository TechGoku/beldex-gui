// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#include "DaemonManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>

#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX  // keep std::max / std::clamp usable
#endif
#include <windows.h>  // CREATE_NO_WINDOW
#endif

#ifndef BELDEX_GUI_DAEMON_HINT
#define BELDEX_GUI_DAEMON_HINT ""
#endif

static DaemonManager *s_daemonManager = nullptr;

DaemonManager::DaemonManager(QObject *parent) : QObject(parent), m_net_(new QNetworkAccessManager(this))
{
    s_daemonManager = this;
    m_binary = findBinary();
    m_poll.setInterval(3000);
    connect(&m_poll, &QTimer::timeout, this, &DaemonManager::poll);
}

DaemonManager::~DaemonManager()
{
    shutdown(5000);
    if (s_daemonManager == this)
        s_daemonManager = nullptr;
}

DaemonManager *DaemonManager::create(QQmlEngine *, QJSEngine *)
{
    auto *d = s_daemonManager ? s_daemonManager : new DaemonManager(nullptr);
    QQmlEngine::setObjectOwnership(d, QQmlEngine::CppOwnership);
    return d;
}

DaemonManager *DaemonManager::instance() { return s_daemonManager; }

quint16 DaemonManager::defaultRpcPort(int net)
{
    return net == 1 ? 29091 : net == 2 ? 39091 : 19091;  // cryptonote_config.h RPC_DEFAULT_PORT
}

QString DaemonManager::netName(int net) { return net == 1 ? QStringLiteral("testnet") : net == 2 ? QStringLiteral("devnet") : QStringLiteral("mainnet"); }

QString DaemonManager::rpcAddress() const { return QStringLiteral("127.0.0.1:%1").arg(defaultRpcPort(m_net)); }

double DaemonManager::syncProgress() const
{
    if (synchronized())
        return 1.0;
    if (!m_target)
        return 0.0;
    return std::clamp(double(m_height) / double(m_target), 0.0, 1.0);
}

QString DaemonManager::statusText() const
{
    if (!m_running)
        return m_starting ? tr("Starting local node…") : tr("Local node stopped");
    if (m_peers == 0 && !synchronized())
        return m_net == 1 ? tr("Local testnet node can't reach any peers · block %1 (add a testnet peer in Settings › Node)").arg(m_height)
                          : tr("Local node looking for peers · block %1").arg(m_height);
    if (!synchronized())
        return tr("Local node syncing %1% · %2 / %3 · %4 peers")
            .arg(syncProgress() * 100, 0, 'f', 1).arg(m_height).arg(m_target).arg(m_peers);
    return tr("Local node synced · block %1 · %2 peers").arg(m_height).arg(m_peers);
}

QString DaemonManager::logPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(QStringLiteral("beldexd-%1.log").arg(netName(m_net)));
}

QString DaemonManager::defaultDataDir(int net) const
{
    const QString base = QDir::home().filePath(QStringLiteral(".beldex"));
    return net == 1 ? QDir(base).filePath(QStringLiteral("testnet")) : net == 2 ? QDir(base).filePath(QStringLiteral("devnet")) : base;
}

// beldexd lookup: explicit path, next to the app, the bundled Beldex core build, then PATH.
QString DaemonManager::findBinary() const
{
    const QString exe = QStringLiteral("beldexd") + (QSysInfo::productType() == QLatin1String("windows") ? QStringLiteral(".exe") : QString());
    const QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath(exe),
        QStringLiteral(BELDEX_GUI_DAEMON_HINT),
        QStandardPaths::findExecutable(QStringLiteral("beldexd")),
    };
    for (const QString &c : candidates)
        if (!c.isEmpty() && QFileInfo(c).isExecutable())
            return QFileInfo(c).absoluteFilePath();
    return {};
}

void DaemonManager::setBinaryPath(const QString &path)
{
    const QString p = path.trimmed().isEmpty() ? findBinary() : path.trimmed();
    if (p == m_binary)
        return;
    m_binary = p;
    emit binaryChanged();
}

bool DaemonManager::binaryFound() const { return !m_binary.isEmpty() && QFileInfo(m_binary).isExecutable(); }

void DaemonManager::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    if (running)
        m_starting = false;
    emit stateChanged();
    emit statusChanged();
}

void DaemonManager::postRpc(const QString &address, const QString &method,
                            std::function<void(bool, const QJsonObject &)> done, int timeoutMs)
{
    QNetworkRequest req(QUrl(QStringLiteral("http://%1/json_rpc").arg(address)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(timeoutMs);
    const QByteArray body = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", "0"}, {"method", method}}).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_net_->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(false, {});
            return;
        }
        const QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object().value("result").toObject();
        done(!result.isEmpty(), result);
    });
}

void DaemonManager::probe(const QString &address, int net)
{
    const QString want = netName(net);
    postRpc(address, QStringLiteral("get_info"), [this, address, want](bool ok, const QJsonObject &r) {
        if (!ok) {
            emit probed(address, false, 0, tr("%1 is not answering").arg(address));
            return;
        }
        const QString nettype = r.value("nettype").toString(want);
        if (nettype != want) {
            emit probed(address, false, 0, tr("%1 is a %2 node, not %3").arg(address, nettype, want));
            return;
        }
        emit probed(address, true, r.value("height").toDouble(), {});
    });
}

void DaemonManager::start(int net, const QString &dataDir, const QStringList &extraArgs)
{
    if ((m_running || m_starting) && net == m_net) {
        if (m_running && m_announced)
            emit readyForWallet(rpcAddress());
        return;
    }
    if (m_running || m_starting)
        stop();  // switching networks
    m_net = net;
    m_error.clear();
    m_height = m_target = 0;
    m_peers = 0;
    m_announced = false;
    m_starting = true;
    emit stateChanged();
    emit statusChanged();

    // A node may already be running (started outside the app). Use it if it is on the right network.
    postRpc(rpcAddress(), QStringLiteral("get_info"), [this, net, dataDir, extraArgs](bool ok, const QJsonObject &r) {
        if (ok) {
            const QString nettype = r.value("nettype").toString(netName(net));
            if (nettype != netName(net)) {
                m_starting = false;
                m_error = tr("Port %1 is used by a %2 node, not %3").arg(defaultRpcPort(net)).arg(nettype, netName(net));
                emit stateChanged();
                emit error(m_error);
                return;
            }
            m_poll.start();
            poll();
            return;
        }
        if (!binaryFound()) {
            m_starting = false;
            m_error = tr("beldexd not found. Build it (scripts/build-beldex-core.sh) or set its path in Settings › Node.");
            emit stateChanged();
            emit error(m_error);
            return;
        }
        QStringList args{QStringLiteral("--non-interactive"), QStringLiteral("--log-file"), logPath()};
        if (net == 1)
            args << QStringLiteral("--testnet");
        else if (net == 2)
            args << QStringLiteral("--devnet");
        if (!dataDir.trimmed().isEmpty())
            args << QStringLiteral("--data-dir") << dataDir.trimmed();
        args << extraArgs;
        QDir().mkpath(QFileInfo(logPath()).absolutePath());

        auto *proc = new QProcess(this);
#ifdef Q_OS_WIN
        // beldexd is a console program; don't pop up a console window next to the wallet.
        proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
#endif
        proc->setProgram(m_binary);
        proc->setArguments(args);
        proc->setStandardOutputFile(QProcess::nullDevice());   // everything goes to --log-file
        proc->setStandardErrorFile(QProcess::nullDevice());
        connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
            const bool expected = proc->property("stopping").toBool();
            proc->deleteLater();
            m_poll.stop();
            m_starting = false;
            if (!expected) {
                const QString why = lastLogError();
                m_error = why.isEmpty() ? tr("beldexd exited (code %1). See %2").arg(code).arg(logPath())
                                        : tr("beldexd stopped: %1").arg(why);
                emit error(m_error);
            }
            setRunning(false);
            emit stateChanged();
        });
        connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                m_starting = false;
                m_error = tr("Could not start %1: %2").arg(m_binary, proc->errorString());
                emit stateChanged();
                emit error(m_error);
            }
        });
        qInfo("daemon: starting %s %s", qPrintable(m_binary), qPrintable(args.join(' ')));
        m_proc = proc;
        proc->start();
        m_poll.start();
        emit stateChanged();
    });
}

// First real error beldexd wrote to its log in this run (fields are tab-separated: time, thread, level,
// category, location, message), e.g. "Failed to add genesis block to blockchain".
QString DaemonManager::lastLogError() const
{
    QFile f(logPath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    if (f.size() > 65536)
        f.seek(f.size() - 65536);
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    QStringList errors;
    for (const QString &line : lines) {
        if (line.contains(QLatin1String("New log categories")))
            errors.clear();  // a new run started; only report this one
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 6 || (fields[2] != QLatin1String("ERROR") && fields[2] != QLatin1String("FATAL")))
            continue;
        const QString msg = fields.mid(5).join(QLatin1Char(' ')).remove(QRegularExpression(QStringLiteral("\\x1b\\[[0-9;]*m"))).trimmed();
        if (!msg.startsWith(QLatin1String("Failed to deinitialize")) && !errors.contains(msg))
            errors << msg;
    }
    return errors.mid(0, 2).join(QStringLiteral(" · "));
}

void DaemonManager::poll()
{
    postRpc(rpcAddress(), QStringLiteral("get_info"), [this](bool ok, const QJsonObject &r) {
        if (!ok) {
            if (!ownsProcess() && m_running) {  // external node went away
                setRunning(false);
                m_poll.stop();
            }
            return;
        }
        if (r.value("nettype").toString(netName(m_net)) != netName(m_net))
            return;
        m_height = quint64(r.value("height").toDouble());
        m_target = quint64(r.value("target_height").toDouble());
        m_peers = r.value("outgoing_connections_count").toInt() + r.value("incoming_connections_count").toInt();
        setRunning(true);
        emit statusChanged();
        if (!m_announced) {
            m_announced = true;
            emit readyForWallet(rpcAddress());
        }
    });
}

void DaemonManager::stop()
{
    m_poll.stop();
    m_announced = false;
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->setProperty("stopping", true);
        QPointer<QProcess> p = m_proc;
        // Ask beldexd to save and exit (works on every OS). terminate() is the Unix fallback (SIGTERM);
        // on Windows it can't stop a console program, so kill() is only the last resort.
        postRpc(rpcAddress(), QStringLiteral("stop_daemon"), [](bool, const QJsonObject &) {}, 5000);
        QTimer::singleShot(10000, this, [p] {
            if (p && p->state() != QProcess::NotRunning)
                p->terminate();
        });
        QTimer::singleShot(45000, this, [p] {
            if (p && p->state() != QProcess::NotRunning)
                p->kill();
        });
    }
    m_starting = false;
    setRunning(false);
    emit stateChanged();
}

void DaemonManager::shutdown(int ms)
{
    m_poll.stop();
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->setProperty("stopping", true);
        // Same order as stop(), synchronously: RPC stop, then terminate, then kill.
        QEventLoop loop;
        postRpc(rpcAddress(), QStringLiteral("stop_daemon"), [&loop](bool, const QJsonObject &) { loop.quit(); }, 3000);
        loop.exec();
        if (!m_proc->waitForFinished(std::max(1000, ms / 2))) {
            m_proc->terminate();
            if (!m_proc->waitForFinished(ms / 2)) {
                m_proc->kill();
                m_proc->waitForFinished(2000);
            }
        }
    }
}
