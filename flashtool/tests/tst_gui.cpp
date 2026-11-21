// GUI smoke test: the main window builds, shows and renders on this platform.
// Runs on the offscreen platform in CTest; set ACEFLASH_SCREENSHOT_DIR to keep a PNG.
#include <QtTest>
#include <QApplication>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QDir>
#include <QProgressBar>
#include "ui/mainwindow.h"
#include "ui/flash_widget.h"

class TstGui : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
        // Any Qt warning (missing plugin, bad layout, failed connect) fails the test,
        // except the offscreen platform's harmless size-hint notice.
        QTest::failOnWarning(QRegularExpression(
            QStringLiteral("^(?!This plugin does not support propagateSizeHints).*")));
#endif
    }

    void mainWindowShows()
    {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        QVERIFY(w.width() > 200 && w.height() > 200);
        QVERIFY(!w.findChildren<QPushButton*>().isEmpty());
        QVERIFY(!w.findChildren<QLineEdit*>().isEmpty());   // printer IP field

        const QImage shot = w.grab().toImage();
        QVERIFY(!shot.isNull());

        const QString dir = qEnvironmentVariable("ACEFLASH_SCREENSHOT_DIR");
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            const QString path = dir + QStringLiteral("/mainwindow-%1.png")
                                           .arg(QGuiApplication::platformName());
            QVERIFY2(shot.save(path), qPrintable(path));
            qInfo("screenshot: %s", qPrintable(path));
        }
    }

    // Opt-in end-to-end check against a real printer (read-only: Test Connection only).
    //   ACEFLASH_TEST_PRINTER_IP=192.168.1.50 ./tst_gui liveConnectionTest
    void liveConnectionTest()
    {
        const QString ip = qEnvironmentVariable("ACEFLASH_TEST_PRINTER_IP");
        if (ip.isEmpty())
            QSKIP("set ACEFLASH_TEST_PRINTER_IP to run against a printer");

        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        auto* lineIp = w.findChild<QLineEdit*>(QStringLiteral("lineIp"));
        auto* btnTest = w.findChild<QPushButton*>(QStringLiteral("btnDiscover"));
        auto* status = w.findChild<QLabel*>(QStringLiteral("labelStatus"));
        auto* printer = w.findChild<QLabel*>(QStringLiteral("labelPrinter"));
        auto* btnFlash = w.findChild<QPushButton*>(QStringLiteral("btnFlash"));
        auto* logView = w.findChild<QPlainTextEdit*>(QStringLiteral("logView"));
        QVERIFY(lineIp && btnTest && status && printer && btnFlash && logView);

        lineIp->setText(ip);
        QTest::mouseClick(btnTest, Qt::LeftButton);
        // "OK  <model> (<ip>)" = HTTP discovery done; "... connected" = MQTT test done
        QTRY_VERIFY_WITH_TIMEOUT(status->text().endsWith(QLatin1String("connected"))
                                 || status->text().startsWith(QLatin1String("ERR")), 30000);
        QVERIFY2(!status->text().startsWith(QLatin1String("ERR")), qPrintable(status->text()));
        QVERIFY(logView->toPlainText().contains(QLatin1String("Test OK")));
        QVERIFY(printer->text() != QLatin1String("-"));
        QVERIFY(!btnFlash->isEnabled());   // no image selected -> flashing impossible
        qInfo("status: %s | printer: %s", qPrintable(status->text()), qPrintable(printer->text()));

        const QString dir = qEnvironmentVariable("ACEFLASH_SCREENSHOT_DIR");
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            w.grab().toImage().save(dir + QStringLiteral("/mainwindow-live.png"));
        }
    }

    // Opt-in REAL flash through the GUI. Only with an explicit image and printer:
    //   ACEFLASH_TEST_PRINTER_IP=... ACEFLASH_TEST_FLASH_FILE=/fw/ACE_V....bin ./tst_gui liveFlash
    // Behind a firewall also set ACEFLASH_HTTP_PORT (and ACEFLASH_LOCAL_IP in a container).
    void liveFlash()
    {
        const QString ip   = qEnvironmentVariable("ACEFLASH_TEST_PRINTER_IP");
        const QString file = qEnvironmentVariable("ACEFLASH_TEST_FLASH_FILE");
        if (ip.isEmpty() || file.isEmpty())
            QSKIP("set ACEFLASH_TEST_PRINTER_IP and ACEFLASH_TEST_FLASH_FILE to flash");

        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto* lineIp  = w.findChild<QLineEdit*>(QStringLiteral("lineIp"));
        auto* btnTest = w.findChild<QPushButton*>(QStringLiteral("btnDiscover"));
        auto* status  = w.findChild<QLabel*>(QStringLiteral("labelStatus"));
        auto* flash   = w.findChild<FlashWidget*>();
        auto* btnFlash = w.findChild<QPushButton*>(QStringLiteral("btnFlash"));
        auto* bar     = w.findChild<QProgressBar*>(QStringLiteral("progressBar"));
        auto* logView = w.findChild<QPlainTextEdit*>(QStringLiteral("logView"));
        QVERIFY(lineIp && btnTest && status && flash && btnFlash && bar && logView);

        lineIp->setText(ip);
        QTest::mouseClick(btnTest, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(status->text().endsWith(QLatin1String("connected")), 30000);

        flash->loadFirmware(file);
        QVERIFY2(btnFlash->isEnabled(), qPrintable(logView->toPlainText()));   // guards passed
        QTest::mouseClick(btnFlash, Qt::LeftButton);

        const auto done = [&] {
            const QString l = logView->toPlainText();
            return l.contains(QLatin1String("Flash successful")) || l.contains(QLatin1String("Flash failed"));
        };
        QTRY_VERIFY_WITH_TIMEOUT(done(), 10 * 60 * 1000);
        qInfo("%s", qPrintable(logView->toPlainText()));

        const QString dir = qEnvironmentVariable("ACEFLASH_SCREENSHOT_DIR");
        if (!dir.isEmpty()) w.grab().toImage().save(dir + QStringLiteral("/mainwindow-flash.png"));

        QVERIFY(logView->toPlainText().contains(QLatin1String("Flash successful")));
        QCOMPARE(bar->value(), 100);
    }
};

QTEST_MAIN(TstGui)
#include "tst_gui.moc"
