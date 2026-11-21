#pragma once
#include <QObject>
#include <QTcpServer>
#include <QByteArray>
#include <QString>

class HttpServer : public QObject {
    Q_OBJECT
public:
    explicit HttpServer(QObject* parent = nullptr);

    // hintIp: printer IP — used to select the correct local network interface
    // port:   0 = ephemeral (default); fixed port makes firewall rules possible
    // localIp: overrides the auto-detected local address put into url()
    bool    start(const QByteArray& data, const QString& filename,
                  const QString& hintIp = {}, quint16 port = 0,
                  const QString& localIp = {});
    void    stop();
    QString url()  const;
    int     port() const;
    // Download requests received since the last start()
    int     requestCount() const { return m_requests; }

signals:
    void httpLog(const QString& entry);
    void requestReceived(const QString& peer);

private slots:
    void onNewConnection();

private:
    // Returns the local IP that routes to the printer
    static QString localIpFor(const QString& hintIp);

    QTcpServer  m_server;
    QByteArray  m_data;
    QString     m_filename;
    QString     m_localIp;
    int         m_requests = 0;
};
