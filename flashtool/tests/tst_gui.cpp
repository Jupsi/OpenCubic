// GUI smoke test: the main window builds, shows and renders on this platform.
// Runs on the offscreen platform in CTest; set ACEFLASH_SCREENSHOT_DIR to keep a PNG.
#include <QtTest>
#include <QApplication>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QDir>
#include "ui/mainwindow.h"

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
};

QTEST_MAIN(TstGui)
#include "tst_gui.moc"
