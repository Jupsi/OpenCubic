#pragma once
#include <QWidget>
#include <QTimer>
#include <QHash>
#include <memory>
#include "../core/discovery.h"
#include "../core/firmware_file.h"
#include "../core/http_server.h"
#include "../core/mqtt_client.h"
#include "../targets/ams_target.h"
#include "../targets/printer_app_target.h"

namespace Ui { class FlashWidget; }

class FlashWidget : public QWidget {
    Q_OBJECT
public:
    explicit FlashWidget(QWidget* parent = nullptr);
    ~FlashWidget();

    // Called after HTTP discovery — starts MQTT test and keeps the connection alive
    void startTest(const PrinterCredentials& creds);

signals:
    void statusUpdate(const QString& msg);   // → DiscoveryWidget::setStatus

private slots:
    void onBrowseClicked();
    void onFlashClicked();
    void onMqttConnected();
    void onMqttMessage(const QString& topic, const QByteArray& payload);
    void onMqttError(const QString& msg);
    void onTestTimeout();

private:
    // State machine
    enum class State { Idle, Testing, Ready, Flashing };
    void setState(State s);
    void sendTestQueries();
    void tryFinishTest();
    void resetToIdle(const QString& reason);

    // Flash
    void updateFlashButton();
    QString mismatchReason() const;   // empty = image/printer/ACE combination is allowed
    void updateAceIdSpinner();
    void flashDone(bool success);

public slots:
    void appendHttpLog(const QString& entry);

private:
    // Log helpers
    void log(const QString& msg);
    void appendMqttLog(const QString& entry);

    // Topic helpers
    QString pubTopic(const QString& suffix) const;
    QString subTopic(const QString& suffix) const;

    Ui::FlashWidget*   ui;
    PrinterCredentials m_creds;
    FirmwareFile       m_fw;
    HttpServer         m_http;
    MqttClient         m_mqtt;

    // Test state
    State   m_state       = State::Idle;
    bool    m_gotAceInfo  = false;
    bool    m_printerFree = true;
    int     m_aceCount    = 0;
    QHash<int, int> m_aceModelIds;   // ACE id -> model_id (40001/40002), if reported by the printer
    QTimer  m_testTimer;

    // Flash state
    QString    m_otaTopic;
    QString    m_reportTopic;
    QByteArray m_otaPayload;
};
