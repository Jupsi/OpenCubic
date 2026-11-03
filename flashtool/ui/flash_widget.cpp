#include "flash_widget.h"
#include "ui_flash_widget.h"
#include "../core/firmware_packager.h"
#include <QFileDialog>
#include <QFile>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QUuid>
#include <QCryptographicHash>

// Builds the MQTT envelope: type, action, timestamp, msgid, data
static QByteArray makeEnvelope(const QString& type,
                                const QString& action,
                                const QJsonValue& data = QJsonValue::Null)
{
    QJsonObject obj;
    obj[QStringLiteral("type")]      = type;
    obj[QStringLiteral("action")]    = action;
    obj[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
    obj[QStringLiteral("msgid")]     = QUuid::createUuid().toString(QUuid::WithoutBraces);
    obj[QStringLiteral("data")]      = data;
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

static constexpr int TEST_TIMEOUT_MS = 15000;

// ── Constructor ───────────────────────────────────────────────────────────────

FlashWidget::FlashWidget(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::FlashWidget)
{
    ui->setupUi(this);
    connect(ui->btnBrowse, &QPushButton::clicked, this, &FlashWidget::onBrowseClicked);
    connect(ui->btnFlash,  &QPushButton::clicked, this, &FlashWidget::onFlashClicked);
    connect(ui->spinAceId, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { updateFlashButton(); });

    connect(&m_mqtt, &MqttClient::connected,       this, &FlashWidget::onMqttConnected);
    connect(&m_mqtt, &MqttClient::messageReceived, this, &FlashWidget::onMqttMessage);
    connect(&m_mqtt, &MqttClient::error,           this, &FlashWidget::onMqttError);
    connect(&m_mqtt, &MqttClient::mqttLog,         this, &FlashWidget::appendMqttLog);

    m_testTimer.setSingleShot(true);
    connect(&m_testTimer, &QTimer::timeout, this, &FlashWidget::onTestTimeout);
}

FlashWidget::~FlashWidget() { delete ui; }

// ── Topics ────────────────────────────────────────────────────────────────────

QString FlashWidget::pubTopic(const QString& suffix) const
{
    return QStringLiteral("anycubic/anycubicCloud/v1/public/printer/%1/%2/%3")
        .arg(m_creds.modelId, m_creds.deviceId, suffix);
}

QString FlashWidget::subTopic(const QString& suffix) const
{
    return QStringLiteral("anycubic/anycubicCloud/v1/printer/public/%1/%2/%3")
        .arg(m_creds.modelId, m_creds.deviceId, suffix);
}

// ── State Machine ─────────────────────────────────────────────────────────────

void FlashWidget::setState(State s)
{
    m_state = s;
    updateFlashButton();
}

void FlashWidget::updateAceIdSpinner()
{
    // Max is constrained by firmware type if an ACE firmware is loaded
    int maxByFw = (m_fw.target == FlashTarget::AceGen1) ? 1 : 3;

    // Show spinner only when multiple ACEs are actually connected.
    // Before test (m_aceCount == 0): hide — no information yet.
    bool show  = (m_aceCount > 1);
    int  maxId = (m_aceCount > 1) ? qMin(maxByFw, m_aceCount - 1) : maxByFw;

    ui->spinAceId->setMaximum(maxId);
    if (ui->spinAceId->value() > maxId)
        ui->spinAceId->setValue(maxId);
    ui->spinAceId->setVisible(show);
    ui->labelAceIdLbl->setVisible(show);
}

QString FlashWidget::mismatchReason() const
{
    const bool isAce = (m_fw.target == FlashTarget::AceGen1 || m_fw.target == FlashTarget::AceGen2);
    if (!isAce) return {};

    // AVATA stack only drives ACE 2 Pro (Gen 2).
    // KlipperGo stack drives both generations (newer printer FW, e.g. Kobra S1 Max with ACE 2 Pro);
    // the OTA container is identical for ACE_V... and ACE2_V... images.
    if (m_creds.stack() == PrinterStack::Avata && m_fw.target == FlashTarget::AceGen1)
        return QStringLiteral("AVATA stack (ACE 2 Pro) needs an ACE2_V... image.");

    // If the printer reports the hub model, the image must match the selected ACE.
    const int aceId = ui->spinAceId->value();
    const int hubModel = m_aceModelIds.value(aceId, 0);
    const int fwModel  = (m_fw.target == FlashTarget::AceGen2) ? 40002 : 40001;
    if ((hubModel == 40001 || hubModel == 40002) && hubModel != fwModel)
        return QStringLiteral("ACE%1 is an %2, but the image is for an %3.")
            .arg(aceId)
            .arg(hubModel == 40002 ? QStringLiteral("ACE 2 Pro (Gen 2)") : QStringLiteral("ACE 1 Pro (Gen 1)"))
            .arg(fwModel  == 40002 ? QStringLiteral("ACE 2 Pro (ACE2_V...)") : QStringLiteral("ACE 1 Pro (ACE_V...)"));

    return {};
}

void FlashWidget::updateFlashButton()
{
    // Enable flashing unless the image does not match the printer stack / connected ACE.
    ui->btnFlash->setEnabled(
        m_fw.isValid()
        && mismatchReason().isEmpty()
        && m_state == State::Ready
        && m_printerFree);
}

void FlashWidget::resetToIdle(const QString& reason)
{
    m_testTimer.stop();
    m_http.stop();
    setState(State::Idle);
    m_gotAceInfo      = false;
    m_printerFree     = true;
    log(QStringLiteral("RESET: ") + reason);
    emit statusUpdate(QStringLiteral("Disconnected - Test Connection required"));
}

// ── Test entry (after HTTP discovery) ────────────────────────────────────────

void FlashWidget::startTest(const PrinterCredentials& creds)
{
    m_creds = creds;

    // Disconnect any previous connection
    m_mqtt.disconnectFromBroker();
    m_testTimer.stop();
    m_gotAceInfo      = false;
    m_printerFree     = true;
    m_aceCount        = 0;
    m_aceModelIds.clear();
    updateAceIdSpinner();

    ui->logView->clear();
    ui->labelPrinter->setText(QStringLiteral("%1  (%2)  [%3]")
        .arg(creds.modelName, creds.ip,
             creds.stack() == PrinterStack::KlipperGo ? "klipper-go" : "avata"));

    if (creds.stack() == PrinterStack::Avata) {
        // AVATA stack = ACE Gen 2 — only ACE2_V... images are valid here.
        log(QStringLiteral("AVATA stack detected — ACE Gen 2. Select an ACE2_V... (Gen 2) CFW image."));
        log(QStringLiteral("Do NOT flash an ACE_V... (Gen 1) image on this printer."));
    }

    setState(State::Testing);
    log(QStringLiteral("-- Test Connection (%1:%2) --------")
        .arg(creds.mqttHost()).arg(creds.mqttPort()));

    m_testTimer.start(TEST_TIMEOUT_MS);
    m_mqtt.connectToBroker(
        creds.mqttHost(), creds.mqttPort(),
        creds.mqttUser,   creds.mqttPass,
        QStringLiteral("aceflash"),
        creds.devicecrt,  creds.devicepk);
}

// ── MQTT callbacks ────────────────────────────────────────────────────────────

void FlashWidget::onMqttConnected()
{
    log(QStringLiteral("MQTT connected - starting test queries..."));

    // Persistent monitoring topics
    m_mqtt.subscribe(subTopic(QStringLiteral("lastWill/report")));
    m_mqtt.subscribe(subTopic(QStringLiteral("status/report")));

    // Test topics
    m_mqtt.subscribe(subTopic(QStringLiteral("ota/report")));
    m_mqtt.subscribe(subTopic(QStringLiteral("info/report")));
    m_mqtt.subscribe(subTopic(QStringLiteral("multiColorBox/report")));
    for (int i = 0; i < 4; ++i)
        m_mqtt.subscribe(subTopic(QStringLiteral("ota/multiColorBox/%1/report").arg(i)));

    // Send queries
    QTimer::singleShot(400, this, &FlashWidget::sendTestQueries);
}

void FlashWidget::sendTestQueries()
{
    m_mqtt.publish(pubTopic(QStringLiteral("multiColorBox")),
                   makeEnvelope(QStringLiteral("multiColorBox"), QStringLiteral("getInfo")));
    m_mqtt.publish(pubTopic(QStringLiteral("info")),
                   makeEnvelope(QStringLiteral("info"), QStringLiteral("query")));
}

void FlashWidget::onMqttMessage(const QString& topic, const QByteArray& payload)
{
    auto    obj  = QJsonDocument::fromJson(payload).object();
    QString typ  = obj[QStringLiteral("type")].toString();
    QString act  = obj[QStringLiteral("action")].toString();
    QString st   = obj[QStringLiteral("state")].toString();
    auto    dat  = obj[QStringLiteral("data")].toObject();

    // ── Persistent monitoring ─────────────────────────────────────────────────

    // Printer offline
    if (topic.endsWith(QStringLiteral("lastWill/report")) && st == QStringLiteral("offline")) {
        resetToIdle(QStringLiteral("Printer offline (lastWill)"));
        return;
    }

    // Printer status (busy/free)
    if (topic.endsWith(QStringLiteral("status/report")) && typ == QStringLiteral("status")) {
        bool wasFree = m_printerFree;
        m_printerFree = (st == QStringLiteral("free"));
        if (wasFree != m_printerFree)
            log(QStringLiteral("[Status] %1").arg(m_printerFree ? "free" : st));
        updateFlashButton();
        return;
    }

    // ── Test phase responses ──────────────────────────────────────────────────
    if (m_state == State::Testing) {

        // Printer info from info/report
        if (topic.endsWith(QStringLiteral("info/report"))) {
            QString ver   = dat[QStringLiteral("version")].toString();
            QString name  = dat[QStringLiteral("printerName")].toString();
            QString state = dat[QStringLiteral("state")].toString();
            auto    temp  = dat[QStringLiteral("temp")].toObject();
            if (!name.isEmpty())  log(QStringLiteral("  Printer      : %1").arg(name));
            if (!ver.isEmpty())   log(QStringLiteral("  FW Version   : %1").arg(ver));
            if (!state.isEmpty()) log(QStringLiteral("  Status       : %1").arg(state));
            int hotbed  = temp[QStringLiteral("curr_hotbed_temp")].toInt();
            int nozzle  = temp[QStringLiteral("curr_nozzle_temp")].toInt();
            log(QStringLiteral("  Temp         : Bed %1 C / Nozzle %2 C").arg(hotbed).arg(nozzle));
        }

        // Printer FW version from ota/report (auto-sent)
        if (topic.endsWith(QStringLiteral("ota/report")) && act == QStringLiteral("reportVersion")) {
            QString ver = dat[QStringLiteral("firmware_version")].toString();
            if (!ver.isEmpty()) log(QStringLiteral("  Printer FW   : %1").arg(ver));
        }

        // ACE unit list from multiColorBox/getInfo including slot details
        // ota:reportVersion does not provide FW version in LAN mode (no cloud connection needed)
        if (topic.endsWith(QStringLiteral("multiColorBox/report"))
                && (act == QStringLiteral("getInfo") || act == QStringLiteral("report"))
                && !m_gotAceInfo) {
            auto boxes = dat[QStringLiteral("multi_color_box")].toArray();
            log(QStringLiteral("  ACE Units    : %1").arg(boxes.size()));
            m_gotAceInfo = true;
            m_aceCount   = boxes.size();
            m_aceModelIds.clear();
            for (int bi = 0; bi < boxes.size(); ++bi) {
                QJsonObject aceBox = boxes[bi].toObject();
                const int aceId   = aceBox[QStringLiteral("id")].toInt();
                const int modelId = aceBox[QStringLiteral("model_id")].toInt(0);   // newer printer FW only
                if (modelId > 0) m_aceModelIds.insert(aceId, modelId);
                const QString modelStr =
                    modelId == 40002 ? QStringLiteral("  model=ACE 2 Pro") :
                    modelId == 40001 ? QStringLiteral("  model=ACE 1 Pro") :
                    modelId > 0      ? QStringLiteral("  model=%1").arg(modelId) : QString();
                log(QStringLiteral("    ACE%1  status=%2  temp=%3 C%4")
                    .arg(aceId)
                    .arg(aceBox[QStringLiteral("status")].toInt())
                    .arg(aceBox[QStringLiteral("temp")].toInt())
                    .arg(modelStr));

                QJsonArray aceSlots = aceBox[QStringLiteral("slots")].toArray();
                for (int si = 0; si < aceSlots.size(); ++si) {
                    QJsonObject sl   = aceSlots[si].toObject();
                    QString slType   = sl[QStringLiteral("type")].toString();
                    int     slStatus = sl[QStringLiteral("status")].toInt();
                    if (!slType.isEmpty() && slStatus != 4) {
                        QString slSku = sl[QStringLiteral("sku")].toString();
                        log(QStringLiteral("      Slot%1  %2  %3  %4%%")
                            .arg(sl[QStringLiteral("index")].toInt())
                            .arg(slType)
                            .arg(slSku.isEmpty() ? QStringLiteral("-") : slSku)
                            .arg(sl[QStringLiteral("consumables_percent")].toInt()));
                    }
                }
            }
            tryFinishTest();
        }
    }

    // ── Flash phase responses ─────────────────────────────────────────────────
    if (m_state == State::Flashing) {
        log(QStringLiteral("[OTA] %1").arg(st));
        if      (st == QStringLiteral("start"))            ui->progressBar->setValue(10);
        else if (st == QStringLiteral("downloading"))      ui->progressBar->setValue(
            10 + dat[QStringLiteral("progress")].toInt(0) * 0.6);
        else if (st == QStringLiteral("download-success")) ui->progressBar->setValue(70);
        else if (st == QStringLiteral("updating")) {
            int p = dat[QStringLiteral("current_progress")].toInt(0);
            ui->progressBar->setValue(70 + p * 0.29);
        }
        else if (st == QStringLiteral("update-success")) flashDone(true);
        else if (st.contains(QStringLiteral("fail")) || st.contains(QStringLiteral("error")))
            flashDone(false);
    }
}

void FlashWidget::tryFinishTest()
{
    if (!m_gotAceInfo) return;
    m_testTimer.stop();
    updateAceIdSpinner();
    log(QStringLiteral("-- Test OK - Connection remains active ---"));
    emit statusUpdate(QStringLiteral("OK  %1  connected").arg(m_creds.modelName));
    setState(State::Ready);
}

void FlashWidget::onTestTimeout()
{
    if (m_gotAceInfo)
        tryFinishTest();
    else {
        log(QStringLiteral("ERR Timeout - no response from printer"));
        resetToIdle(QStringLiteral("Timeout"));
    }
}

void FlashWidget::onMqttError(const QString& msg)
{
    if (m_state == State::Testing)
        resetToIdle(msg);
    else if (m_state == State::Flashing)
        flashDone(false);
    log(QStringLiteral("MQTT ERR: ") + msg);
}

// ── File selection ────────────────────────────────────────────────────────────

void FlashWidget::onBrowseClicked()
{
    QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select Firmware"), {},
        QStringLiteral("Firmware (ACE_V*.bin ACE2_V*.bin *.swu);;All Files (*)"));
    if (path.isEmpty()) return;

    m_fw = FirmwareFile::load(path);
    ui->labelFile->setText(m_fw.name);
    updateAceIdSpinner();
    updateFlashButton();
    const QString mismatch = mismatchReason();
    if (!mismatch.isEmpty())
        log(QStringLiteral("WARN Flashing disabled: ") + mismatch);
}

// ── Flash ─────────────────────────────────────────────────────────────────────

void FlashWidget::onFlashClicked()
{
    if (m_state != State::Ready) return;

    // Safety guard: block image / stack / ACE generation mismatches.
    const QString mismatch = mismatchReason();
    if (!mismatch.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("Error"),
            QStringLiteral("Firmware / printer mismatch — flashing blocked to prevent soft-brick.\n")
            + mismatch);
        return;
    }

    QFile f(m_fw.path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, QStringLiteral("Error"),
                              QStringLiteral("Could not read firmware file."));
        return;
    }
    QByteArray binData = f.readAll();

    int aceId = ui->spinAceId->value();
    std::unique_ptr<IFlashTarget> target;
    if (m_fw.target == FlashTarget::AceGen1 || m_fw.target == FlashTarget::AceGen2) {
        auto t = std::make_unique<AmsTarget>(aceId);
        if (!t->validate(m_fw)) {
            QMessageBox::critical(this, QStringLiteral("Error"),
                QStringLiteral("ACE ID %1 is invalid for this firmware type.").arg(aceId));
            return;
        }
        target = std::move(t);
    } else {
        auto t = std::make_unique<PrinterAppTarget>();
        if (!t->validate(m_fw)) {
            QMessageBox::critical(this, QStringLiteral("Error"),
                                  QStringLiteral("Invalid firmware type."));
            return;
        }
        target = std::move(t);
    }

    QString serveName;
    QByteArray container = FirmwarePackager::package(binData, m_fw.name, m_creds.stack(), serveName);
    if (container.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("Error"), QStringLiteral("Firmware packaging failed."));
        return;
    }

    m_http.stop();
    connect(&m_http, &HttpServer::httpLog, this, &FlashWidget::appendHttpLog,
            Qt::UniqueConnection);
    if (!m_http.start(container, serveName, m_creds.ip)) {
        QMessageBox::critical(this, QStringLiteral("Error"), QStringLiteral("Failed to start HTTP server."));
        return;
    }

    m_otaTopic    = target->otaTopic(m_creds.modelId, m_creds.deviceId);
    m_reportTopic = target->reportTopic(m_creds.modelId, m_creds.deviceId);

    // MD5 and size of the served container (not the original .bin)
    QString containerMd5 = QCryptographicHash::hash(
        container, QCryptographicHash::Md5).toHex();
    m_otaPayload  = target->buildPayload(
        m_fw, m_http.url(), container.size(), containerMd5);

    // Subscribe to OTA report topic
    m_mqtt.subscribe(m_reportTopic);

    setState(State::Flashing);
    ui->progressBar->setValue(0);
    log(QStringLiteral("-- Flash Start ----------------------------"));
    log(QStringLiteral("  Firmware : %1  (%2 Byte)").arg(m_fw.name).arg(container.size()));
    log(QStringLiteral("  URL      : %1").arg(m_http.url()));
    log(QStringLiteral("  OTA-Topic: %1").arg(m_otaTopic));

    QTimer::singleShot(300, this, [this]() {
        m_mqtt.publish(m_otaTopic, m_otaPayload);
        ui->progressBar->setValue(5);
    });
}

void FlashWidget::flashDone(bool success)
{
    m_http.stop();
    setState(State::Ready);   // back to Ready (connection stays active)
    ui->progressBar->setValue(success ? 100 : 0);
    log(success ? QStringLiteral("OK  Flash successful!")
                : QStringLiteral("ERR Flash failed."));
}

// ── Log ───────────────────────────────────────────────────────────────────────

void FlashWidget::log(const QString& msg)
{
    ui->logView->appendPlainText(
        QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss  ")) + msg);
}

void FlashWidget::appendHttpLog(const QString& entry)
{
    ui->logHttp->appendPlainText(
        QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss  ")) + entry);
}

void FlashWidget::appendMqttLog(const QString& entry)
{
    ui->logMqtt->appendPlainText(
        QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss  ")) + entry);
}
