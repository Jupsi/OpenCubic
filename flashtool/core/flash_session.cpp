#include "flash_session.h"
#include "crypto.h"
#include "targets/ams_target.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QUuid>
#include <QFile>

namespace aceflash {

// ── Pure helpers ──────────────────────────────────────────────────────────────

QVector<AceUnit> parseAceInfo(const QJsonObject& data)
{
    QVector<AceUnit> units;
    const QJsonArray boxes = data[QStringLiteral("multi_color_box")].toArray();
    for (const QJsonValue& v : boxes) {
        const QJsonObject box = v.toObject();
        AceUnit u;
        u.id      = box[QStringLiteral("id")].toInt();
        u.status  = box[QStringLiteral("status")].toInt();
        u.temp    = box[QStringLiteral("temp")].toInt();
        u.modelId = box[QStringLiteral("model_id")].toInt(0);   // newer printer FW only
        for (const QJsonValue& sv : box[QStringLiteral("slots")].toArray()) {
            const QJsonObject sl = sv.toObject();
            const QString type = sl[QStringLiteral("type")].toString();
            if (type.isEmpty() || sl[QStringLiteral("status")].toInt() == 4)   // 4 = empty
                continue;
            AceSlot s;
            s.index   = sl[QStringLiteral("index")].toInt();
            s.type    = type;
            s.sku     = sl[QStringLiteral("sku")].toString();
            s.percent = sl[QStringLiteral("consumables_percent")].toInt();
            u.loaded.append(s);
        }
        units.append(u);
    }
    return units;
}

OtaProgress parseOtaReport(const QJsonObject& msg)
{
    OtaProgress p;
    p.state = msg[QStringLiteral("state")].toString();
    const QJsonObject data = msg[QStringLiteral("data")].toObject();
    const QString& st = p.state;

    if (st == QLatin1String("start")) {
        p.percent = 10;
    } else if (st == QLatin1String("downloading")) {
        p.percent = 10 + data[QStringLiteral("progress")].toInt(0) * 60 / 100;
    } else if (st == QLatin1String("download-success")) {
        p.percent = 70;
    } else if (st == QLatin1String("updating")) {
        p.percent = 70 + data[QStringLiteral("current_progress")].toInt(0) * 29 / 100;
    } else if (st == QLatin1String("update-success")) {
        p.percent = 100;
        p.result  = 1;
    } else if (st.contains(QLatin1String("fail")) || st.contains(QLatin1String("error"))) {
        p.result  = -1;
    }
    return p;
}

QString aceModelName(int modelId)
{
    switch (modelId) {
    case 40001: return QStringLiteral("ACE 1 Pro (Gen 1)");
    case 40002: return QStringLiteral("ACE 2 Pro (Gen 2)");
    case 0:     return QStringLiteral("unknown");
    default:    return QStringLiteral("model %1").arg(modelId);
    }
}

QString firmwareMismatch(PrinterStack stack, FlashTarget target, int aceId,
                         const QHash<int, int>& aceModelIds)
{
    const bool isAce = (target == FlashTarget::AceGen1 || target == FlashTarget::AceGen2);
    if (!isAce) return {};

    // AVATA stack only drives ACE 2 Pro (Gen 2).
    if (stack == PrinterStack::Avata && target == FlashTarget::AceGen1)
        return QStringLiteral("AVATA stack (ACE 2 Pro) needs an ACE2_V... image.");

    // If the printer reports the hub model, the image must match the selected ACE.
    const int hubModel = aceModelIds.value(aceId, 0);
    const int fwModel  = (target == FlashTarget::AceGen2) ? 40002 : 40001;
    if ((hubModel == 40001 || hubModel == 40002) && hubModel != fwModel)
        return QStringLiteral("ACE%1 is an %2, but the image is for an %3.")
            .arg(aceId).arg(aceModelName(hubModel), aceModelName(fwModel));

    return {};
}

QString flashPrecheck(PrinterStack stack, FlashTarget target, int aceId,
                      const QVector<AceUnit>& units, bool printerFree)
{
    if (target != FlashTarget::AceGen1 && target != FlashTarget::AceGen2)
        return QStringLiteral("Not an ACE image. File name must be ACE_V<ver>_<date>.bin "
                              "(Gen 1) or ACE2_V<ver>_<date>.bin (Gen 2).");
    if (stack == PrinterStack::Unknown)
        return QStringLiteral("Unknown printer model - container format cannot be chosen safely.");
    if (units.isEmpty())
        return QStringLiteral("Printer reports no connected ACE unit.");

    QHash<int, int> models;
    bool present = false;
    for (const AceUnit& u : units) {
        if (u.id == aceId) present = true;
        if (u.modelId > 0) models.insert(u.id, u.modelId);
    }
    if (!present)
        return QStringLiteral("ACE%1 is not connected (printer reports %2 unit(s)).")
            .arg(aceId).arg(units.size());
    if (!printerFree)
        return QStringLiteral("Printer is busy - wait until it is idle.");

    const QString mismatch = firmwareMismatch(stack, target, aceId, models);
    if (!mismatch.isEmpty())
        return QStringLiteral("Firmware / printer mismatch - blocked to prevent soft-brick: ")
               + mismatch;
    return {};
}

bool isOtaReportFor(const QString& topic, const QString& reportTopic,
                    const QString& type, const QString& action)
{
    if (topic == reportTopic) return true;
    return topic.endsWith(QLatin1String("/ota/report"))
        && type == QLatin1String("ota")
        && action != QLatin1String("reportVersion");
}

QString downloadBlockedHint(const QString& otaState, int requests,
                            const QString& printerIp, quint16 httpPort)
{
    if (requests > 0 || !otaState.contains(QLatin1String("download")))
        return {};
    const QString port = QString::number(httpPort ? httpPort : 8765);
    QString hint = QStringLiteral(
        "The printer never fetched the image from this PC - a firewall here is "
        "probably blocking it. The ACE firmware was not changed.\n");
    if (!httpPort)
        hint += QStringLiteral("Re-run with --http-port %1 and allow that port, e.g.:\n").arg(port);
    else
        hint += QStringLiteral("Allow port %1 from the printer, e.g.:\n").arg(port);
    hint += QStringLiteral(
        "  ufw:       sudo ufw allow from %1 to any port %2 proto tcp\n"
        "  firewalld: sudo firewall-cmd --add-rich-rule='rule family=\"ipv4\" source "
        "address=\"%1\" port port=\"%2\" protocol=\"tcp\" accept'\n"
        "See docs/linux-cli.md#firewall.").arg(printerIp, port);
    return hint;
}

static QByteArray makeEnvelope(const QString& type, const QString& action)
{
    QJsonObject obj;
    obj[QStringLiteral("type")]      = type;
    obj[QStringLiteral("action")]    = action;
    obj[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
    obj[QStringLiteral("msgid")]     = QUuid::createUuid().toString(QUuid::WithoutBraces);
    obj[QStringLiteral("data")]      = QJsonValue::Null;
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// ── FlashSession ──────────────────────────────────────────────────────────────

FlashSession::FlashSession(SessionOptions opts, QObject* parent)
    : QObject(parent), m_opts(opts)
{
    m_timer.setSingleShot(true);

    connect(&m_discovery, &Discovery::credentialsReady, this, &FlashSession::onCredentials);
    connect(&m_discovery, &Discovery::error, this, [this](const QString& msg) {
        finishTest(false, QStringLiteral("Discovery: ") + msg);
    });
    connect(&m_discovery, &Discovery::httpLog, this, &FlashSession::trace);

    connect(&m_mqtt, &MqttClient::connected,       this, &FlashSession::onMqttConnected);
    connect(&m_mqtt, &MqttClient::messageReceived, this, &FlashSession::onMqttMessage);
    connect(&m_mqtt, &MqttClient::error,           this, &FlashSession::onMqttError);
    connect(&m_mqtt, &MqttClient::mqttLog,         this, &FlashSession::trace);
    connect(&m_http, &HttpServer::httpLog,         this, &FlashSession::trace);
    connect(&m_http, &HttpServer::requestReceived, this, [this](const QString& peer) {
        emit log(QStringLiteral("  Printer %1 is downloading the image").arg(peer));
    });

    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (m_state == State::Discovering || m_state == State::Testing)
            finishTest(false, QStringLiteral("Timeout - no response from printer"));
        else if (m_state == State::Flashing)
            finishFlash(false, QStringLiteral("Timeout - no OTA result from printer"));
    });
}

QString FlashSession::pubTopic(const QString& suffix) const
{
    return QStringLiteral("anycubic/anycubicCloud/v1/public/printer/%1/%2/%3")
        .arg(m_creds.modelId, m_creds.deviceId, suffix);
}

QString FlashSession::subTopic(const QString& suffix) const
{
    return QStringLiteral("anycubic/anycubicCloud/v1/printer/public/%1/%2/%3")
        .arg(m_creds.modelId, m_creds.deviceId, suffix);
}

QHash<int, int> FlashSession::aceModelIds() const
{
    QHash<int, int> h;
    for (const AceUnit& u : m_units)
        if (u.modelId > 0) h.insert(u.id, u.modelId);
    return h;
}

void FlashSession::startTest(const QString& ip)
{
    m_mqtt.disconnectFromBroker();
    m_units.clear();
    m_gotAceInfo  = false;
    m_printerFree = true;
    m_printerFw.clear();
    m_ip    = ip;
    m_state = State::Discovering;
    m_timer.start(m_opts.testTimeoutMs);
    emit log(QStringLiteral("Discovery on %1:18910 ...").arg(ip));
    m_discovery.discover(ip);
}

void FlashSession::onCredentials(const PrinterCredentials& creds)
{
    if (m_state != State::Discovering) return;
    m_creds = creds;
    const PrinterStack stack = creds.stack();
    emit log(QStringLiteral("Printer: %1  model=%2  stack=%3")
        .arg(creds.modelName, creds.modelId,
             stack == PrinterStack::KlipperGo ? QStringLiteral("klipper-go")
             : stack == PrinterStack::Avata   ? QStringLiteral("avata")
                                              : QStringLiteral("unknown")));
    m_state = State::Testing;
    emit log(QStringLiteral("MQTT connect %1:%2 ...").arg(creds.mqttHost()).arg(creds.mqttPort()));
    m_mqtt.connectToBroker(creds.mqttHost(), creds.mqttPort(),
                           creds.mqttUser, creds.mqttPass,
                           QStringLiteral("aceflash"),
                           creds.devicecrt, creds.devicepk);
}

void FlashSession::onMqttConnected()
{
    emit log(QStringLiteral("MQTT connected - querying printer"));
    m_mqtt.subscribe(subTopic(QStringLiteral("lastWill/report")));
    m_mqtt.subscribe(subTopic(QStringLiteral("status/report")));
    m_mqtt.subscribe(subTopic(QStringLiteral("ota/report")));
    m_mqtt.subscribe(subTopic(QStringLiteral("info/report")));
    m_mqtt.subscribe(subTopic(QStringLiteral("multiColorBox/report")));
    for (int i = 0; i < 4; ++i)
        m_mqtt.subscribe(subTopic(QStringLiteral("ota/multiColorBox/%1/report").arg(i)));

    QTimer::singleShot(400, this, [this]() {
        m_mqtt.publish(pubTopic(QStringLiteral("multiColorBox")),
                       makeEnvelope(QStringLiteral("multiColorBox"), QStringLiteral("getInfo")));
        m_mqtt.publish(pubTopic(QStringLiteral("info")),
                       makeEnvelope(QStringLiteral("info"), QStringLiteral("query")));
    });
}

void FlashSession::onMqttMessage(const QString& topic, const QByteArray& payload)
{
    const QJsonObject obj = QJsonDocument::fromJson(payload).object();
    const QString typ = obj[QStringLiteral("type")].toString();
    const QString act = obj[QStringLiteral("action")].toString();
    const QString st  = obj[QStringLiteral("state")].toString();
    const QJsonObject dat = obj[QStringLiteral("data")].toObject();

    if (topic.endsWith(QLatin1String("lastWill/report")) && st == QLatin1String("offline")) {
        if (m_state == State::Flashing)
            finishFlash(false, QStringLiteral("Printer went offline"));
        else
            finishTest(false, QStringLiteral("Printer offline (lastWill)"));
        return;
    }

    if (topic.endsWith(QLatin1String("status/report")) && typ == QLatin1String("status")) {
        const bool wasFree = m_printerFree;
        m_printerFree = (st == QLatin1String("free"));
        if (wasFree != m_printerFree)
            emit log(QStringLiteral("Printer status: %1").arg(st));
        return;
    }

    if (m_state == State::Testing) {
        if (topic.endsWith(QLatin1String("/info/report"))) {
            const QString name = dat[QStringLiteral("printerName")].toString();
            const QString ver  = dat[QStringLiteral("version")].toString();
            if (!name.isEmpty()) emit log(QStringLiteral("  Printer name : %1").arg(name));
            if (!ver.isEmpty()) {
                m_printerFw = ver;
                emit log(QStringLiteral("  Printer FW   : %1").arg(ver));
            }
        }
        if (topic.endsWith(QLatin1String("/ota/report")) && act == QLatin1String("reportVersion")) {
            const QString ver = dat[QStringLiteral("firmware_version")].toString();
            if (!ver.isEmpty() && m_printerFw.isEmpty()) m_printerFw = ver;
        }
        if (topic.endsWith(QLatin1String("/multiColorBox/report"))
                && (act == QLatin1String("getInfo") || act == QLatin1String("report"))
                && !m_gotAceInfo) {
            m_units = parseAceInfo(dat);
            m_gotAceInfo = true;
            emit log(QStringLiteral("  ACE units    : %1").arg(m_units.size()));
            for (const AceUnit& u : m_units) {
                emit log(QStringLiteral("    ACE%1  %2  status=%3  temp=%4 C")
                    .arg(u.id).arg(aceModelName(u.modelId)).arg(u.status).arg(u.temp));
                for (const AceSlot& s : u.loaded)
                    emit log(QStringLiteral("      Slot%1  %2  %3  %4%")
                        .arg(s.index).arg(s.type, s.sku.isEmpty() ? QStringLiteral("-") : s.sku)
                        .arg(s.percent));
            }
            // Short grace period so info/report can arrive too
            QTimer::singleShot(800, this, [this]() {
                if (m_state == State::Testing) finishTest(true, {});
            });
        }
        return;
    }

    if (m_state == State::Flashing) {
        if (!isOtaReportFor(topic, m_reportTopic, typ, act)) {
            emit trace(QStringLiteral("(ignored during flash) ") + topic);
            return;
        }
        const OtaProgress p = parseOtaReport(obj);
        if (p.state.isEmpty()) return;
        emit log(QStringLiteral("[OTA] %1").arg(p.state));
        if (p.state == QLatin1String("updating")) m_updateStarted = true;
        if (p.percent >= 0) emit flashProgress(p.percent, p.state);
        if (p.result == 1) {
            finishFlash(true, {});
        } else if (p.result == -1) {
            QString why = QStringLiteral("Printer reported: ") + p.state;
            const QString hint = downloadBlockedHint(p.state, m_http.requestCount(),
                                                     m_ip, m_opts.httpPort);
            if (!hint.isEmpty()) why += QLatin1Char('\n') + hint;
            finishFlash(false, why);
        }
    }
}

void FlashSession::onMqttError(const QString& msg)
{
    if (m_state == State::Discovering || m_state == State::Testing)
        finishTest(false, msg);
    else if (m_state == State::Flashing)
        finishFlash(false, msg);
    else
        emit log(QStringLiteral("MQTT error: ") + msg);
}

void FlashSession::finishTest(bool ok, const QString& error)
{
    if (m_state != State::Discovering && m_state != State::Testing) return;
    m_timer.stop();
    if (ok) {
        m_state = State::Ready;
    } else {
        m_mqtt.disconnectFromBroker();
        m_state = State::Idle;
    }
    emit testFinished(ok, error);
}

bool FlashSession::startFlash(const FirmwareFile& fw, int aceId, QString* error)
{
    auto fail = [error](const QString& msg) { if (error) *error = msg; return false; };

    if (m_state != State::Ready)
        return fail(QStringLiteral("Connection test has not completed."));
    const QString blocked =
        flashPrecheck(m_creds.stack(), fw.target, aceId, m_units, m_printerFree);
    if (!blocked.isEmpty())
        return fail(blocked);

    AmsTarget target(aceId);
    if (!target.validate(fw))
        return fail(QStringLiteral("ACE ID %1 is invalid for this firmware type.").arg(aceId));

    QFile f(fw.path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Could not read %1").arg(fw.path));
    const QByteArray binData = f.readAll();

    QString serveName;
    const QByteArray container =
        FirmwarePackager::package(binData, fw.name, m_creds.stack(), serveName);
    if (container.isEmpty())
        return fail(QStringLiteral("Firmware packaging failed."));

    m_http.stop();
    if (!m_http.start(container, serveName, m_ip, m_opts.httpPort, m_opts.localIp))
        return fail(QStringLiteral("Failed to start HTTP server (port %1).").arg(m_opts.httpPort));

    const QString otaTopic = target.otaTopic(m_creds.modelId, m_creds.deviceId);
    m_reportTopic = target.reportTopic(m_creds.modelId, m_creds.deviceId);
    const QByteArray payload = target.buildPayload(
        fw, m_http.url(), container.size(), md5Hex(container));

    m_mqtt.subscribe(m_reportTopic);
    m_updateStarted = false;
    m_state = State::Flashing;
    m_timer.start(m_opts.flashTimeoutMs);

    emit log(QStringLiteral("Flash start: %1 -> ACE%2").arg(fw.name).arg(aceId));
    emit log(QStringLiteral("  Container : %1 bytes").arg(container.size()));
    emit log(QStringLiteral("  Serving   : %1").arg(m_http.url()));
    emit flashProgress(0, QStringLiteral("publish"));

    QTimer::singleShot(300, this, [this, otaTopic, payload]() {
        m_mqtt.publish(otaTopic, payload);
        emit flashProgress(5, QStringLiteral("requested"));
    });
    return true;
}

void FlashSession::finishFlash(bool ok, const QString& error)
{
    if (m_state != State::Flashing) return;
    m_timer.stop();
    m_http.stop();
    m_state = State::Ready;
    emit flashFinished(ok, error);
}

void FlashSession::disconnectFromPrinter()
{
    m_timer.stop();
    m_http.stop();
    m_mqtt.disconnectFromBroker();
    m_state = State::Idle;
}

} // namespace aceflash
