#pragma once
// UI-free flash orchestration: discovery → MQTT test → package/serve → OTA.
// Logic mirrors ui/flash_widget.cpp so the CLI behaves like the GUI.
#include <QObject>
#include <QTimer>
#include <QHash>
#include <QVector>
#include <QJsonObject>
#include <memory>
#include "discovery.h"
#include "firmware_file.h"
#include "firmware_packager.h"
#include "http_server.h"
#include "mqtt_client.h"

namespace aceflash {

struct AceSlot {
    int     index   = 0;
    QString type;
    QString sku;
    int     percent = 0;
};

struct AceUnit {
    int     id      = 0;
    int     status  = 0;
    int     temp    = 0;
    int     modelId = 0;   // 40001 = ACE 1 Pro, 40002 = ACE 2 Pro, 0 = not reported
    QVector<AceSlot> loaded;   // loaded slots only ("slots" is a Qt macro)
};

// data object of a multiColorBox getInfo/report message
QVector<AceUnit> parseAceInfo(const QJsonObject& data);

struct OtaProgress {
    QString state;
    int     percent = -1;   // overall 0..100, -1 = no change
    int     result  = 0;    // 0 running, 1 success, -1 failure
};

// Full OTA report message (top-level "state", "data")
OtaProgress parseOtaReport(const QJsonObject& msg);

// Soft-brick guard. Empty string = allowed.
QString firmwareMismatch(PrinterStack stack, FlashTarget target, int aceId,
                         const QHash<int, int>& aceModelIds);

QString aceModelName(int modelId);

// Every guard that must pass before an ACE OTA is started. Empty string = allowed.
// Covers: ACE image type, known printer stack, ACE present, printer idle, generation match.
QString flashPrecheck(PrinterStack stack, FlashTarget target, int aceId,
                      const QVector<AceUnit>& units, bool printerFree);

// True if an MQTT message is an OTA progress report for the running flash:
// the exact ACE report topic, or the printer-level ota/report (not reportVersion).
bool isOtaReportFor(const QString& topic, const QString& reportTopic,
                    const QString& type, const QString& action);

// Explanation for a failed OTA download when the printer never fetched the image
// from this PC (requests == 0): almost always a local firewall. Empty = no hint.
// httpPort is the configured --http-port (0 = random, a fixed port is suggested).
QString downloadBlockedHint(const QString& otaState, int requests,
                            const QString& printerIp, quint16 httpPort);

struct SessionOptions {
    int     testTimeoutMs  = 15000;
    int     flashTimeoutMs = 10 * 60 * 1000;
    quint16 httpPort       = 0;   // 0 = ephemeral
    QString localIp;              // empty = auto (route towards printer)
};

class FlashSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Discovering, Testing, Ready, Flashing };
    Q_ENUM(State)

    explicit FlashSession(SessionOptions opts = {}, QObject* parent = nullptr);

    // Discovery on port 18910, then MQTT test queries. Emits testFinished().
    void startTest(const QString& ip);

    // Requires State::Ready. Returns false (and sets *error) if not startable.
    bool startFlash(const FirmwareFile& fw, int aceId, QString* error);

    State                     state()       const { return m_state; }
    const PrinterCredentials& credentials() const { return m_creds; }
    const QVector<AceUnit>&   aceUnits()    const { return m_units; }
    QHash<int, int>           aceModelIds() const;
    bool                      printerFree() const { return m_printerFree; }
    QString                   printerFwVersion() const { return m_printerFw; }
    // True once the printer reported the ACE write phase ("updating") in this flash.
    // False after a failure means the ACE firmware was not touched.
    bool                      updateStarted() const { return m_updateStarted; }

    void disconnectFromPrinter();

signals:
    void log(const QString& line);      // human-readable progress
    void trace(const QString& line);    // raw HTTP / MQTT traffic
    void testFinished(bool ok, const QString& error);
    void flashProgress(int percent, const QString& state);
    void flashFinished(bool ok, const QString& error);

private:
    void onCredentials(const PrinterCredentials& creds);
    void onMqttConnected();
    void onMqttMessage(const QString& topic, const QByteArray& payload);
    void onMqttError(const QString& msg);
    void finishTest(bool ok, const QString& error);
    void finishFlash(bool ok, const QString& error);

    QString pubTopic(const QString& suffix) const;
    QString subTopic(const QString& suffix) const;

    SessionOptions     m_opts;
    Discovery          m_discovery;
    MqttClient         m_mqtt;
    HttpServer         m_http;
    QTimer             m_timer;

    State              m_state       = State::Idle;
    PrinterCredentials m_creds;
    QVector<AceUnit>   m_units;
    bool               m_gotAceInfo  = false;
    bool               m_printerFree = true;
    QString            m_printerFw;
    QString            m_reportTopic;
    QString            m_ip;   // user-supplied printer IP (known reachable)
    bool               m_updateStarted = false;
};

} // namespace aceflash
