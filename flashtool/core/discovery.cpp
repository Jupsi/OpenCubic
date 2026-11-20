#include "discovery.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QRandomGenerator>
#include <QCryptographicHash>

static QString prettyJson(const QByteArray& data)
{
    auto doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return QString::fromUtf8(data);
    return doc.toJson(QJsonDocument::Indented).trimmed();
}
// AES-128-CBC backend per platform:
//   Windows: BCrypt (system) — official release builds
//   macOS:   CommonCrypto (system)
//   Linux:   OpenSSL libcrypto
// macOS/Linux support contributed by ozgunawesome (PR #6); community-supported.
#if defined(Q_OS_WIN)
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "Bcrypt.lib")
#elif defined(Q_OS_MACOS)
#include <CommonCrypto/CommonCryptor.h>
#else
#include <openssl/evp.h>
#endif

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

QString Discovery::md5Hex(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex();
}

// ── AES-128-CBC Decrypt (PKCS#7 padding) ──────────────────────────────────────

QByteArray Discovery::aes128CbcDecrypt(const QByteArray& cipher,
                                        const QByteArray& key,
                                        const QByteArray& iv)
{
    if (cipher.isEmpty() || cipher.size() % 16 != 0)
        return {};

    // Writable, exactly 16-byte copies (BCrypt modifies the IV)
    QByteArray ivCopy = iv.left(16).leftJustified(16, '\0');
    QByteArray keyCopy = key.left(16).leftJustified(16, '\0');

#if defined(Q_OS_MACOS)
    QByteArray plain(cipher.size() + 16, '\0');
    size_t plainLen = 0;
    const CCCryptorStatus ccStatus = CCCrypt(
        kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding,
        keyCopy.constData(), kCCKeySizeAES128,
        ivCopy.constData(),
        cipher.constData(), static_cast<size_t>(cipher.size()),
        plain.data(), static_cast<size_t>(plain.size()), &plainLen);
    if (ccStatus != kCCSuccess)
        return {};
    plain.resize(static_cast<int>(plainLen));
    return plain;

#elif !defined(Q_OS_WIN)
    QByteArray plain(cipher.size() + 16, '\0');
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};
    int decryptedLen = 0;
    int finalLen = 0;
    const bool ok =
        EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(keyCopy.constData()),
                           reinterpret_cast<const unsigned char*>(ivCopy.constData())) == 1 &&
        EVP_DecryptUpdate(ctx,
                          reinterpret_cast<unsigned char*>(plain.data()), &decryptedLen,
                          reinterpret_cast<const unsigned char*>(cipher.constData()),
                          cipher.size()) == 1 &&
        EVP_DecryptFinal_ex(ctx,
                            reinterpret_cast<unsigned char*>(plain.data()) + decryptedLen,
                            &finalLen) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return {};
    plain.resize(decryptedLen + finalLen);
    return plain;

#else
    BCRYPT_ALG_HANDLE  hAlg  = nullptr;
    BCRYPT_KEY_HANDLE  hKey  = nullptr;
    NTSTATUS           status;

    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) return {};

    // Set CBC chaining mode
    const wchar_t* cbcMode = BCRYPT_CHAIN_MODE_CBC;
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                      (PUCHAR)cbcMode,
                      (ULONG)((wcslen(cbcMode) + 1) * sizeof(wchar_t)), 0);

    // Import the key
    ULONG   keyObjLen = 0;
    ULONG   cbResult  = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
                      (PUCHAR)&keyObjLen, sizeof(ULONG), &cbResult, 0);

    QByteArray keyObj(keyObjLen, '\0');
    status = BCryptGenerateSymmetricKey(
        hAlg, &hKey,
        (PUCHAR)keyObj.data(), keyObjLen,
        (PUCHAR)keyCopy.data(), 16, 0);

    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    // Decrypt
    QByteArray plain(cipher.size(), '\0');
    ULONG plainLen = 0;
    status = BCryptDecrypt(
        hKey,
        (PUCHAR)cipher.constData(), (ULONG)cipher.size(),
        nullptr,
        (PUCHAR)ivCopy.data(), 16,
        (PUCHAR)plain.data(), (ULONG)plain.size(),
        &plainLen, BCRYPT_BLOCK_PADDING);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) return {};
    plain.resize(plainLen);
    return plain;
#endif
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
    QString sign  = md5Hex((md5Hex(token.left(16).toLatin1()) + ts + nonce).toLatin1());

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

        QByteArray plain = aes128CbcDecrypt(cipher, key, iv);
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
