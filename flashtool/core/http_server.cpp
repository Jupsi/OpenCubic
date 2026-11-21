#include "http_server.h"
#include <QTcpSocket>
#include <QUdpSocket>
#include <QNetworkInterface>
#include <QHostAddress>

HttpServer::HttpServer(QObject* parent) : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

// Determines the local IP via a UDP routing trick:
// QUdpSocket::connectToHost() selects the correct interface internally without a real connection
QString HttpServer::localIpFor(const QString& hintIp)
{
    if (!hintIp.isEmpty()) {
        QUdpSocket probe;
        probe.connectToHost(hintIp, 80);
        if (probe.localAddress() != QHostAddress::Null &&
                probe.localAddress() != QHostAddress::LocalHost) {
            return probe.localAddress().toString();
        }
    }
    // Fallback: first non-loopback IPv4
    for (const QHostAddress& a : QNetworkInterface::allAddresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol
                && a != QHostAddress::LocalHost)
            return a.toString();
    }
    return QStringLiteral("127.0.0.1");
}

bool HttpServer::start(const QByteArray& data, const QString& filename,
                        const QString& hintIp, quint16 port,
                        const QString& localIp)
{
    m_data     = data;
    m_filename = filename;
    m_localIp  = localIp.isEmpty() ? localIpFor(hintIp) : localIp;
    m_requests = 0;

    if (!m_server.listen(QHostAddress::AnyIPv4, port)) {
        emit httpLog(QStringLiteral("HTTP server listen failed: ")
                     + m_server.errorString());
        return false;
    }
    emit httpLog(QStringLiteral("HTTP server started: %1  (%2 bytes)")
                 .arg(url()).arg(data.size()));
    return true;
}

void HttpServer::stop()
{
    m_server.close();
}

QString HttpServer::url() const
{
    return QStringLiteral("http://%1:%2/%3")
        .arg(m_localIp)
        .arg(m_server.serverPort())
        .arg(m_filename);
}

int HttpServer::port() const { return m_server.serverPort(); }

void HttpServer::onNewConnection()
{
    QTcpSocket* socket = m_server.nextPendingConnection();
    if (!socket) return;

    emit httpLog(QStringLiteral("HTTP << connection from %1:%2")
                 .arg(socket->peerAddress().toString())
                 .arg(socket->peerPort()));

    connect(socket, &QAbstractSocket::errorOccurred, this,
            [this, socket](QAbstractSocket::SocketError) {
                emit httpLog(QStringLiteral("HTTP ERR socket: ") + socket->errorString());
                socket->deleteLater();
            });

    // readyRead: receive HTTP request, send response, then close
    // Store the connection handle so we can disconnect after the first fire (one-shot handler)
    auto* conn = new QMetaObject::Connection;
    *conn = connect(socket, &QTcpSocket::readyRead, this, [this, socket, conn]() {
        QObject::disconnect(*conn);   // respond only once
        delete conn;

        QString req = QString::fromLatin1(socket->readAll()).section("\r\n", 0, 0);
        emit httpLog(QStringLiteral("HTTP >> %1").arg(req));
        ++m_requests;
        emit requestReceived(socket->peerAddress().toString());

        QByteArray header;
        header += "HTTP/1.1 200 OK\r\n";
        header += "Content-Type: application/octet-stream\r\n";
        header += QStringLiteral("Content-Length: %1\r\n").arg(m_data.size()).toLatin1();
        header += "Connection: close\r\n";
        header += "\r\n";

        socket->write(header);
        socket->write(m_data);
        // disconnectFromHost() waits until all buffered data has been sent before closing
        socket->disconnectFromHost();

        emit httpLog(QStringLiteral("HTTP << 200  %1 bytes sent").arg(m_data.size()));
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
}
