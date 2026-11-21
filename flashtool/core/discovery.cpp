#include "discovery.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QRandomGenerator>
#include "crypto.h"

static QString prettyJson(const QByteArray& data)
{
    auto doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return QString::fromUtf8(data);
    return doc.toJson(QJsonDocument::Indented).trimmed();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

QString PrinterCredentials::mqttHost() const
{
    // "mqtts://192.168.1.25:9883" → "192.168.1.25"
    QString s = mqttBroker;
    s.remove(QStringLiteral("mqtts://")).remove(QStringLiteral("mqtt://"));
    return s.section(':', 0, 0);
}

quint16 PrinterCredentials::mqttPort() const
{
    QString s = mqttBroker;
    int idx = s.lastIndexOf(':');
    return (idx >= 0) ? s.mid(idx + 1).toUShort() : 9883;
}

// ── Discovery flow ────────────────────────────────────────────────────────────

Discovery::Discovery(QObject* parent) : QObject(parent) {}

void Discovery::discover(const QString& ip)
{
    QNetworkRequest req{QUrl(QStringLiteral("http://%1:18910/info").arg(ip))};
    req.setTransferTimeout(8000);
    emit httpLog(QStringLiteral(">> GET %1").arg(req.url().toString()));
    auto* reply = m_nam.get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, ip]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit httpLog(QStringLiteral("<< ERR %1").arg(reply->errorString()));
            emit error(QStringLiteral("GET /info failed: ") + reply->errorString());
            return;
        }
        QByteArray body = reply->readAll();
        emit httpLog(QStringLiteral("<< %1\n%2")
            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
            .arg(prettyJson(body)));
        auto obj = QJsonDocument::fromJson(body).object();
        QString token = obj[QStringLiteral("token")].toString();
        if (token.size() < 32) {
            emit error(QStringLiteral("Invalid /info token (length < 32)"));
            return;
        }
        postCtrl(ip, token);
    });
}

void Discovery::postCtrl(const QString& ip, const QString& token)
{
    // sign = MD5( MD5(token[0:16]) + ts + nonce )
    QString ts    = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString nonce = QString::number(QRandomGenerator::global()->bounded(1000000, 9999999));
    QString sign  = aceflash::ctrlSign(token, ts, nonce);

    QUrl url(QStringLiteral("http://%1:18910/ctrl").arg(ip));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("ts"),    ts);
    q.addQueryItem(QStringLiteral("nonce"), nonce);
    q.addQueryItem(QStringLiteral("did"),   QStringLiteral("aceflash"));
    q.addQueryItem(QStringLiteral("sign"),  sign);
    url.setQuery(q);

    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(8000);
    emit httpLog(QStringLiteral(">> POST %1").arg(url.toString()));
    auto* reply = m_nam.post(req, QByteArray{});

    connect(reply, &QNetworkReply::finished, this, [this, reply, token]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit httpLog(QStringLiteral("<< ERR %1").arg(reply->errorString()));
            emit error(QStringLiteral("POST /ctrl failed: ") + reply->errorString());
            return;
        }
        QByteArray body = reply->readAll();
        emit httpLog(QStringLiteral("<< %1\n%2")
            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
            .arg(prettyJson(body)));
        auto outer = QJsonDocument::fromJson(body).object();
        auto data  = outer[QStringLiteral("data")].toObject();
        QByteArray encB64 = data[QStringLiteral("info")].toString().toLatin1();
        QByteArray ivStr  = data[QStringLiteral("token")].toString().toLatin1();

        QByteArray cipher = QByteArray::fromBase64(encB64);
        QByteArray key    = token.mid(16, 16).toLatin1();   // bytes [16:32]
        QByteArray iv     = ivStr.left(16);

        QByteArray plain = aceflash::aes128CbcDecrypt(cipher, key, iv);
        if (plain.isEmpty()) {
            emit error(QStringLiteral("AES decryption failed"));
            return;
        }

        auto j = QJsonDocument::fromJson(plain).object();
        PrinterCredentials creds;
        creds.ip          = j[QStringLiteral("ip")].toString();
        creds.deviceId    = j[QStringLiteral("deviceId")].toString();
        creds.mqttUser    = j[QStringLiteral("username")].toString();
        creds.mqttPass    = j[QStringLiteral("password")].toString();
        creds.mqttBroker  = j[QStringLiteral("broker")].toString();
        creds.modelId     = j[QStringLiteral("modeId")].toString();
        creds.modelName   = j[QStringLiteral("modelName")].toString();
        creds.devicecrt   = j[QStringLiteral("devicecrt")].toString();
        creds.devicepk    = j[QStringLiteral("devicepk")].toString();

        if (creds.deviceId.isEmpty()) {
            emit error(QStringLiteral("Decrypted data incomplete"));
            return;
        }
        emit credentialsReady(creds);
    });
}
