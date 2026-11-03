#pragma once
#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include "firmware_packager.h"

struct PrinterCredentials {
    QString deviceId;
    QString mqttUser;
    QString mqttPass;
    QString mqttBroker;   // "mqtts://192.168.x.x:9883"
    QString modelId;
    QString modelName;
    QString ip;
    QString devicecrt;    // TLS client certificate (PEM)
    QString devicepk;     // TLS client private key (PEM)

    PrinterStack stack() const { return printer_stack_from_model_id(modelId); }

    // Extracts host from "mqtts://host:port"
    QString mqttHost() const;
    quint16 mqttPort() const;
};

class Discovery : public QObject {
    Q_OBJECT
public:
    explicit Discovery(QObject* parent = nullptr);

    // Starts port-18910 flow: GET /info → POST /ctrl → AES-CBC decrypt
    void discover(const QString& ip);

signals:
    void credentialsReady(const PrinterCredentials& creds);
    void error(const QString& message);
    void httpLog(const QString& entry);   // ">> GET url" / "<< 200 body..."

private:
    void postCtrl(const QString& ip, const QString& token);
    static QByteArray aes128CbcDecrypt(const QByteArray& cipher,
                                        const QByteArray& key,
                                        const QByteArray& iv);
    static QString md5Hex(const QByteArray& data);

    QNetworkAccessManager m_nam;
};
