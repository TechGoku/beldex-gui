// Copyright (c) 2026, The Beldex Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQmlEngine>
#include <QTimer>

class QNetworkAccessManager;

// Runs and watches a local beldexd (like monero-gui's DaemonManager), and probes remote nodes.
//
//  * start(net) launches beldexd for that network with its own data dir and log, RPC on 127.0.0.1.
//    If something already answers on that RPC port with the right network, it is used as-is.
//  * get_info is polled every few seconds: height, target height, peers, synced.
//  * stop() asks the node to exit cleanly (stop_daemon RPC, then SIGTERM, then kill).
//  * probe(address, net) checks whether a remote node answers and is on the right network.
class DaemonManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool running READ running NOTIFY stateChanged)          // RPC answers
    Q_PROPERTY(bool starting READ starting NOTIFY stateChanged)        // process up, RPC not yet
    Q_PROPERTY(bool ownsProcess READ ownsProcess NOTIFY stateChanged)  // we launched it
    Q_PROPERTY(int netType READ netType NOTIFY stateChanged)
    Q_PROPERTY(QString rpcAddress READ rpcAddress NOTIFY stateChanged)
    Q_PROPERTY(double height READ height NOTIFY statusChanged)
    Q_PROPERTY(double targetHeight READ targetHeight NOTIFY statusChanged)
    Q_PROPERTY(bool synchronized READ synchronized NOTIFY statusChanged)
    Q_PROPERTY(double syncProgress READ syncProgress NOTIFY statusChanged)
    Q_PROPERTY(int peers READ peers NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString binaryPath READ binaryPath WRITE setBinaryPath NOTIFY binaryChanged)
    Q_PROPERTY(bool binaryFound READ binaryFound NOTIFY binaryChanged)
    Q_PROPERTY(QString logPath READ logPath NOTIFY stateChanged)

public:
    explicit DaemonManager(QObject *parent);
    ~DaemonManager() override;
    static DaemonManager *create(QQmlEngine *, QJSEngine *);
    static DaemonManager *instance();

    static quint16 defaultRpcPort(int net);   // 19091 / 29091 / 39091

    bool running() const { return m_running; }
    bool starting() const { return m_starting; }
    bool ownsProcess() const { return m_proc && m_proc->state() != QProcess::NotRunning; }
    int netType() const { return m_net; }
    QString rpcAddress() const;
    double height() const { return double(m_height); }
    double targetHeight() const { return double(m_target); }
    // A node that has never reached a peer reports target 0 at block 1; that is "no network", not synced.
    bool synchronized() const
    {
        if (!m_running || m_height <= 1)
            return false;
        return m_target == 0 ? m_peers > 0 : m_height >= m_target;
    }
    double syncProgress() const;
    int peers() const { return m_peers; }
    QString statusText() const;
    QString lastError() const { return m_error; }
    QString binaryPath() const { return m_binary; }
    void setBinaryPath(const QString &path);
    bool binaryFound() const;
    QString logPath() const;

    // dataDir empty = ~/.beldex (beldexd default, plus testnet/ or devnet/ per network).
    // extraArgs: e.g. ["--add-peer", "1.2.3.4:29090", "--prune-blockchain"].
    Q_INVOKABLE void start(int netType, const QString &dataDir = {}, const QStringList &extraArgs = {});
    Q_INVOKABLE void stop();
    Q_INVOKABLE void probe(const QString &address, int netType);
    Q_INVOKABLE QString defaultDataDir(int netType) const;

    // Called on app exit: stop a node we started, waiting up to `ms`.
    void shutdown(int ms = 20000);

signals:
    void stateChanged();
    void statusChanged();
    void binaryChanged();
    void readyForWallet(const QString &rpcAddress);     // RPC answers on the requested network
    void probed(const QString &address, bool ok, double height, const QString &message);
    void error(const QString &message);

private:
    void poll();
    void postRpc(const QString &address, const QString &method, std::function<void(bool ok, const QJsonObject &result)> done,
                 int timeoutMs = 4000);
    void setRunning(bool running);
    QString findBinary() const;
    QString lastLogError() const;
    static QString netName(int net);

    QNetworkAccessManager *m_net_ = nullptr;
    QPointer<QProcess> m_proc;
    QTimer m_poll;
    int m_net = 0;
    bool m_running = false, m_starting = false, m_announced = false;
    quint64 m_height = 0, m_target = 0;
    int m_peers = 0;
    QString m_error, m_binary, m_dataDir;
};
