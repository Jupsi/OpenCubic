#include "mqtt_client.h"
#include <qmqtt.h>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QSslError>
#include <QUuid>
#include <QJsonDocument>

// Returns pretty-printed JSON; falls back to raw text for invalid JSON
static QString prettyJson(const QByteArray& data)
{
    auto doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return QString::fromUtf8(data);
    return doc.toJson(QJsonDocument::Indented).trimmed();
}

MqttClient::MqttClient(QObject* parent) : QObject(parent) {}
MqttClient::~MqttClient() = default;

void MqttClient::connectToBroker(const QString& host, quint16 port,
                                  const QString& user, const QString& pass,
                                  const QString& clientId,
                                  const QString& certPem,
                                  const QString& keyPem)
{
    if (!QSslSocket::supportsSsl()) {
#if defined(Q_OS_WIN)
        emit error(QStringLiteral(
            "SSL not available — OpenSSL DLLs missing. "
            "libssl-1_1-x64.dll and libcrypto-1_1-x64.dll must be placed next to the .exe."));
#else
        emit error(QStringLiteral(
            "SSL not available — Qt Network has no TLS backend. "
            "Install the OpenSSL runtime that matches your Qt 5 build."));
#endif
        return;
    }

    // Permissive SSL config for LAN device with self-signed certificate
    QSslConfiguration sslConfig;
    sslConfig.setProtocol(QSsl::AnyProtocol);
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);

    // Optional client certificates from discovery (devicecrt / devicepk)
    if (!certPem.isEmpty() && !keyPem.isEmpty()) {
        QSslCertificate cert(certPem.toUtf8());
        QSslKey         key(keyPem.toUtf8(), QSsl::Rsa);
        if (!cert.isNull() && !key.isNull()) {
            sslConfig.setLocalCertificate(cert);
            sslConfig.setPrivateKey(key);
        }
    }

    m_client = std::make_unique<QMQTT::Client>(host, port, sslConfig,
                                                /*ignoreSelfSigned=*/true, this);
    m_client->setClientId(clientId.isEmpty()
        ? QStringLiteral("aceflash-") + QUuid::createUuid().toString(QUuid::Id128).left(8)
        : clientId);
    m_client->setUsername(user);
    m_client->setPassword(pass.toUtf8());
    m_client->setKeepAlive(60);
    m_client->setCleanSession(true);

    connect(m_client.get(), &QMQTT::Client::connected,
            this,           &MqttClient::connected);   // existing signal forward
    connect(m_client.get(), &QMQTT::Client::disconnected,
            this,           &MqttClient::disconnected);
    emit mqttLog(QStringLiteral(">> CONNECT %1:%2  user=%3").arg(host).arg(port).arg(user));

    connect(m_client.get(), &QMQTT::Client::connected, this, [this]() {
        emit mqttLog(QStringLiteral("<< CONNECTED"));
    });

    connect(m_client.get(), &QMQTT::Client::received, this,
            [this](const QMQTT::Message& msg) {
                emit mqttLog(QStringLiteral("<< SUB %1\n%2")
                    .arg(msg.topic(), prettyJson(msg.payload())));
                emit messageReceived(msg.topic(), msg.payload());
            });
    connect(m_client.get(), &QMQTT::Client::error, this,
            [this](const QMQTT::ClientError err) {
                // Human-readable descriptions for the most common SSL/TCP errors
                QString desc;
                switch (static_cast<int>(err)) {
                case  1: desc = QStringLiteral("Connection refused");                           break;
                case  2: desc = QStringLiteral("Remote host disconnected");                     break;
                case  3: desc = QStringLiteral("Host not found");                               break;
                case  6: desc = QStringLiteral("Timeout");                                      break;
                case 14: desc = QStringLiteral("SSL handshake failed");                         break;
                case 21: desc = QStringLiteral("SSL internal — OpenSSL DLL error or protocol incompatible"); break;
                default: desc = QStringLiteral("Code %1").arg(static_cast<int>(err));           break;
                }
                emit error(QStringLiteral("MQTT: ") + desc);
            });

    m_client->connectToHost();
}

void MqttClient::subscribe(const QString& topic)
{
    if (!m_client) return;
    emit mqttLog(QStringLiteral(">> SUB %1").arg(topic));
    m_client->subscribe(topic, 1);   // QoS 1: at-least-once
}

void MqttClient::publish(const QString& topic, const QByteArray& payload)
{
    if (!m_client) return;
    emit mqttLog(QStringLiteral(">> PUB %1\n%2")
        .arg(topic, prettyJson(payload)));
    m_client->publish(QMQTT::Message(0, topic, payload, 1));  // QoS 1
}

void MqttClient::disconnectFromBroker()
{
    if (m_client) m_client->disconnectFromHost();
}
