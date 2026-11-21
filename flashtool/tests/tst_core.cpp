// Unit tests for the UI-free flash core (QtTest).
// Run: ctest --test-dir build --output-on-failure
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>

#include "core/crypto.h"
#include "core/firmware_file.h"
#include "core/firmware_packager.h"
#include "core/flash_session.h"
#include "targets/ams_target.h"

class TstCore : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tmp;

    QString writeFile(const QString& name, const QByteArray& data)
    {
        const QString path = m_tmp.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return {};
        f.write(data);
        return path;
    }

private slots:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    // ── crypto ──────────────────────────────────────────────────────────────

    void aesDecrypt_knownVector()
    {
        // Generated with:
        //   printf '<plain>' | openssl enc -aes-128-cbc
        //     -K 30313233343536373839616263646566 -iv 66656463626139383736353433323130 | base64
        const QByteArray cipher = QByteArray::fromBase64(
            "a8sndvxaKaz2jwjoSMrUHHykktEpzGjKoWvfX0do9U8K9pYf3dLFe7eeQjnxtQigjrivYhaT2k6fYrVu5qnBRA==");
        const QByteArray plain = aceflash::aes128CbcDecrypt(
            cipher, QByteArrayLiteral("0123456789abcdef"), QByteArrayLiteral("fedcba9876543210"));
        QCOMPARE(plain, QByteArrayLiteral(R"({"deviceId":"abc123","username":"u","password":"p"})"));
    }

    void aesDecrypt_wrongKeyFails()
    {
        const QByteArray cipher = QByteArray::fromBase64(
            "a8sndvxaKaz2jwjoSMrUHHykktEpzGjKoWvfX0do9U8K9pYf3dLFe7eeQjnxtQigjrivYhaT2k6fYrVu5qnBRA==");
        const QByteArray plain = aceflash::aes128CbcDecrypt(
            cipher, QByteArrayLiteral("XXXXXXXXXXXXXXXX"), QByteArrayLiteral("fedcba9876543210"));
        // Wrong key → padding check fails (or garbage); must never yield the real plaintext
        QVERIFY(!plain.startsWith("{\"deviceId\""));
    }

    void ctrlSign_matchesReference()
    {
        // printf '%s' "$(printf 'TOKENTOKENTOKEN1' | md5sum | cut -d' ' -f1)17000000000001234567" | md5sum
        const QString token = QStringLiteral("TOKENTOKENTOKEN1") + QString(16, QLatin1Char('k'));
        QCOMPARE(aceflash::ctrlSign(token, QStringLiteral("1700000000000"), QStringLiteral("1234567")),
                 QStringLiteral("8b4e3b314038afc3f9172264bb8ce285"));
    }

    // ── firmware file detection ─────────────────────────────────────────────

    void firmwareFile_target_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("target");
        QTest::newRow("gen1")    << "ACE_V1.3.863_20250518.bin"  << int(FlashTarget::AceGen1);
        QTest::newRow("gen2")    << "ACE2_V1.1.31_20260306.bin"  << int(FlashTarget::AceGen2);
        QTest::newRow("swu")     << "K3_V2.4.0_update.swu"       << int(FlashTarget::PrinterApp);
        QTest::newRow("renamed") << "version.bin"                << int(FlashTarget::Unknown);
    }

    void firmwareFile_target()
    {
        QFETCH(QString, name);
        QFETCH(int, target);
        const QString path = writeFile(name, QByteArrayLiteral("payload"));
        const FirmwareFile fw = FirmwareFile::load(path);
        QCOMPARE(int(fw.target), target);
        QCOMPARE(fw.md5, QString::fromLatin1(
            QCryptographicHash::hash("payload", QCryptographicHash::Md5).toHex()));
    }

    void firmwareFile_missing()
    {
        QVERIFY(!FirmwareFile::load(m_tmp.filePath("nope.bin")).isValid());
    }

    // ── packaging ───────────────────────────────────────────────────────────

    void packager_avataIsRaw()
    {
        QString serve;
        const QByteArray bin = QByteArrayLiteral("\x01\x02\x03rawbin");
        QCOMPARE(FirmwarePackager::package(bin, "ACE2_V1.0.7_20261115.bin",
                                           PrinterStack::Avata, serve), bin);
        QCOMPARE(serve, QStringLiteral("ACE2_V1.0.7_20261115.bin"));
    }

    void packager_klipperGoRoundTrip()
    {
        // Verifies the ZipCrypto+ustar container with the system unzip/tar,
        // i.e. independent implementations of both formats.
        const QString unzip = QStandardPaths::findExecutable(QStringLiteral("unzip"));
        const QString tar   = QStandardPaths::findExecutable(QStringLiteral("tar"));
        if (unzip.isEmpty() || tar.isEmpty())
            QSKIP("unzip/tar not installed");

        QByteArray bin(70000, '\0');
        for (int i = 0; i < bin.size(); ++i) bin[i] = char(i * 31 + 7);
        const QString binName = QStringLiteral("ACE_V1.1.2_20261115.bin");

        QString serve;
        const QByteArray zip = FirmwarePackager::package(bin, binName, PrinterStack::KlipperGo, serve);
        QVERIFY(!zip.isEmpty());
        QCOMPARE(serve, binName);
        const QString zipPath = writeFile(QStringLiteral("container.zip"), zip);

        QProcess p;
        p.start(unzip, {QStringLiteral("-P"), QString::fromLatin1(aceflash::kKlipperZipPassword),
                        QStringLiteral("-p"), zipPath, QStringLiteral("update_swu/setup.tar")});
        QVERIFY(p.waitForFinished());
        QCOMPARE(p.exitCode(), 0);
        const QByteArray tarData = p.readAllStandardOutput();
        const QString tarPath = writeFile(QStringLiteral("setup.tar"), tarData);

        QProcess t;
        t.start(tar, {QStringLiteral("-xOf"), tarPath, binName});
        QVERIFY(t.waitForFinished());
        QCOMPARE(t.exitCode(), 0);
        QCOMPARE(t.readAllStandardOutput(), bin);
    }

    // ── printer stack ───────────────────────────────────────────────────────

    void stack_fromModelId()
    {
        QCOMPARE(printer_stack_from_model_id("20026"), PrinterStack::KlipperGo);
        QCOMPARE(printer_stack_from_model_id("20029"), PrinterStack::KlipperGo);
        QCOMPARE(printer_stack_from_model_id("20030"), PrinterStack::Avata);
        QCOMPARE(printer_stack_from_model_id("99999"), PrinterStack::Unknown);
    }

    // ── mismatch guard (soft-brick protection) ──────────────────────────────

    void mismatch_data()
    {
        QTest::addColumn<int>("stack");
        QTest::addColumn<int>("target");
        QTest::addColumn<int>("hubModel");   // 0 = not reported
        QTest::addColumn<bool>("blocked");
        const int K = int(PrinterStack::KlipperGo), A = int(PrinterStack::Avata);
        const int G1 = int(FlashTarget::AceGen1), G2 = int(FlashTarget::AceGen2);
        QTest::newRow("klipper gen1 unknown hub")  << K << G1 << 0     << false;
        QTest::newRow("klipper gen2 unknown hub")  << K << G2 << 0     << false;
        QTest::newRow("klipper gen1 on gen1 hub")  << K << G1 << 40001 << false;
        QTest::newRow("klipper gen1 on gen2 hub")  << K << G1 << 40002 << true;
        QTest::newRow("klipper gen2 on gen1 hub")  << K << G2 << 40001 << true;
        QTest::newRow("avata gen1")                << A << G1 << 0     << true;
        QTest::newRow("avata gen2")                << A << G2 << 40002 << false;
    }

    void mismatch()
    {
        QFETCH(int, stack);
        QFETCH(int, target);
        QFETCH(int, hubModel);
        QFETCH(bool, blocked);
        QHash<int, int> models;
        if (hubModel) models.insert(0, hubModel);
        const QString reason = aceflash::firmwareMismatch(
            PrinterStack(stack), FlashTarget(target), /*aceId=*/0, models);
        QCOMPARE(!reason.isEmpty(), blocked);
    }

    void mismatch_nonAceTargetIsNotBlockedHere()
    {
        QVERIFY(aceflash::firmwareMismatch(PrinterStack::Avata, FlashTarget::PrinterApp, 0, {}).isEmpty());
    }

    // ── flash precheck (all guards before any network action) ──────────────

    void precheck_data()
    {
        QTest::addColumn<int>("stack");
        QTest::addColumn<int>("target");
        QTest::addColumn<int>("aceId");
        QTest::addColumn<int>("units");      // number of reported ACE units (ids 0..n-1)
        QTest::addColumn<int>("hubModel");   // model of ACE0..n, 0 = not reported
        QTest::addColumn<bool>("free");
        QTest::addColumn<bool>("blocked");
        const int K = int(PrinterStack::KlipperGo), A = int(PrinterStack::Avata), U = int(PrinterStack::Unknown);
        const int G1 = int(FlashTarget::AceGen1), G2 = int(FlashTarget::AceGen2), SW = int(FlashTarget::PrinterApp);
        QTest::newRow("ok klipper gen1")      << K << G1 << 0 << 1 << 40001 << true  << false;
        QTest::newRow("ok avata gen2 ace1")   << A << G2 << 1 << 2 << 40002 << true  << false;
        QTest::newRow("unknown stack")        << U << G2 << 0 << 1 << 40002 << true  << true;
        QTest::newRow("no ace connected")     << K << G1 << 0 << 0 << 0     << true  << true;
        QTest::newRow("ace id out of range")  << K << G2 << 1 << 1 << 40002 << true  << true;
        QTest::newRow("printer busy")         << K << G1 << 0 << 1 << 40001 << false << true;
        QTest::newRow("not an ace image")     << K << SW << 0 << 1 << 40001 << true  << true;
        QTest::newRow("gen mismatch")         << K << G1 << 0 << 1 << 40002 << true  << true;
    }

    void precheck()
    {
        QFETCH(int, stack); QFETCH(int, target); QFETCH(int, aceId);
        QFETCH(int, units); QFETCH(int, hubModel); QFETCH(bool, free); QFETCH(bool, blocked);
        QVector<aceflash::AceUnit> list;
        for (int i = 0; i < units; ++i) {
            aceflash::AceUnit u;
            u.id = i;
            u.modelId = hubModel;
            list.append(u);
        }
        const QString reason = aceflash::flashPrecheck(
            PrinterStack(stack), FlashTarget(target), aceId, list, free);
        QCOMPARE(!reason.isEmpty(), blocked);
    }

    // ── OTA report routing ──────────────────────────────────────────────────

    void otaTopicAccepted()
    {
        const QString mine = "x/printer/public/1/D/ota/multiColorBox/0/report";
        QVERIFY(aceflash::isOtaReportFor(mine, mine, "ota", "update"));
        QVERIFY(aceflash::isOtaReportFor("x/printer/public/1/D/ota/report", mine, "ota", "update"));
        QVERIFY(!aceflash::isOtaReportFor("x/printer/public/1/D/ota/report", mine, "ota", "reportVersion"));
        QVERIFY(!aceflash::isOtaReportFor("x/printer/public/1/D/ota/multiColorBox/1/report", mine, "ota", "update"));
        QVERIFY(!aceflash::isOtaReportFor("x/printer/public/1/D/info/report", mine, "info", "query"));
    }

    // ── MQTT payload parsing ────────────────────────────────────────────────

    void parseAceInfo_units()
    {
        const QJsonObject data = QJsonDocument::fromJson(R"({
            "multi_color_box": [
              {"id":0,"status":1,"temp":25,"model_id":40002,
               "slots":[{"index":0,"type":"PLA","sku":"AC-1","status":5,"consumables_percent":80},
                        {"index":1,"type":"","status":4}]},
              {"id":1,"status":1,"temp":24}
            ]})").object();
        const auto units = aceflash::parseAceInfo(data);
        QCOMPARE(units.size(), 2);
        QCOMPARE(units[0].id, 0);
        QCOMPARE(units[0].modelId, 40002);
        QCOMPARE(units[0].loaded.size(), 1);          // empty slot (status 4) skipped
        QCOMPARE(units[0].loaded[0].type, QStringLiteral("PLA"));
        QCOMPARE(units[0].loaded[0].percent, 80);
        QCOMPARE(units[1].modelId, 0);
    }

    void parseOtaReport_data()
    {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<int>("percent");
        QTest::addColumn<int>("result");   // 0 running, 1 success, -1 failure
        QTest::newRow("start")    << QByteArray(R"({"state":"start"})")                                    << 10 << 0;
        QTest::newRow("dl 50")    << QByteArray(R"({"state":"downloading","data":{"progress":50}})")       << 40 << 0;
        QTest::newRow("dl ok")    << QByteArray(R"({"state":"download-success"})")                         << 70 << 0;
        QTest::newRow("upd 100")  << QByteArray(R"({"state":"updating","data":{"current_progress":100}})") << 99 << 0;
        QTest::newRow("ok")       << QByteArray(R"({"state":"update-success"})")                           << 100 << 1;
        QTest::newRow("dl fail")  << QByteArray(R"({"state":"download-failed"})")                          << -1 << -1;
        QTest::newRow("error")    << QByteArray(R"({"state":"update-error"})")                             << -1 << -1;
        QTest::newRow("no state") << QByteArray(R"({"type":"status"})")                                    << -1 << 0;
    }

    void parseOtaReport()
    {
        QFETCH(QByteArray, payload);
        QFETCH(int, percent);
        QFETCH(int, result);
        const auto r = aceflash::parseOtaReport(QJsonDocument::fromJson(payload).object());
        QCOMPARE(r.percent, percent);
        QCOMPARE(r.result, result);
    }

    // ── OTA payload ─────────────────────────────────────────────────────────

    void amsTarget_payloadAndTopics()
    {
        const QString path = writeFile(QStringLiteral("ACE2_V1.0.7_20261115.bin"), "x");
        const FirmwareFile fw = FirmwareFile::load(path);
        AmsTarget t(1);
        QVERIFY(t.validate(fw));
        QCOMPARE(t.otaTopic("20029", "DEV"),
                 QStringLiteral("anycubic/anycubicCloud/v1/public/printer/20029/DEV/ota/multiColorBox/1"));
        QCOMPARE(t.reportTopic("20029", "DEV"),
                 QStringLiteral("anycubic/anycubicCloud/v1/printer/public/20029/DEV/ota/multiColorBox/1/report"));

        const auto root = QJsonDocument::fromJson(
            t.buildPayload(fw, "http://1.2.3.4:5/f.bin", 123, "abc")).object();
        QCOMPARE(root["type"].toString(), QStringLiteral("ota"));
        QCOMPARE(root["action"].toString(), QStringLiteral("update"));
        const auto d = root["data"].toObject();
        QCOMPARE(d["model_id"].toInt(), 40002);
        QCOMPARE(d["firmware_version"].toString(), QStringLiteral("1.0.7"));
        QCOMPARE(d["firmware_size"].toInt(), 123);
        QCOMPARE(d["firmware_md5"].toString(), QStringLiteral("abc"));
    }

    void amsTarget_aceIdRange()
    {
        const FirmwareFile g1 = FirmwareFile::load(writeFile("ACE_V1.1.2_20261115.bin", "x"));
        QVERIFY(AmsTarget(1).validate(g1));
        QVERIFY(!AmsTarget(2).validate(g1));
        const FirmwareFile g2 = FirmwareFile::load(writeFile("ACE2_V1.0.7_20261115.bin", "x"));
        QVERIFY(AmsTarget(3).validate(g2));
        QVERIFY(!AmsTarget(4).validate(g2));
    }
};

QTEST_GUILESS_MAIN(TstCore)
#include "tst_core.moc"
