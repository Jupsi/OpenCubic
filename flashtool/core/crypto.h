#pragma once
#include <QByteArray>
#include <QString>

namespace aceflash {

// ZIP password klipper-go uses for OTA containers (from ota.go)
inline constexpr const char* kKlipperZipPassword =
    "U2FsdGVkX19deTfqpXHZnB5GeyQ/dtlbHjkUnwgCi+w=";

// AES-128-CBC with PKCS#7 padding. Key/IV are truncated / zero-padded to 16 bytes.
// Returns an empty array on failure (e.g. bad padding).
QByteArray aes128CbcDecrypt(const QByteArray& cipher,
                            const QByteArray& key,
                            const QByteArray& iv);

QString md5Hex(const QByteArray& data);

// Port 18910 /ctrl signature: MD5( MD5(token[0:16]) + ts + nonce )
QString ctrlSign(const QString& token, const QString& ts, const QString& nonce);

} // namespace aceflash
