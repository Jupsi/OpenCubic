// aceflash — headless ACE Pro firmware flasher (Linux / macOS / Windows)
//
//   aceflash test  --ip 192.168.1.50
//   aceflash flash --ip 192.168.1.50 --file ACE2_V1.0.7_20261115.bin
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QDateTime>
#include <csignal>
#include "core/flash_session.h"

#ifndef ACEFLASH_VERSION
#define ACEFLASH_VERSION "dev"
#endif

using aceflash::FlashSession;

namespace {

enum ExitCode {
    ExitOk       = 0,
    ExitUsage    = 1,
    ExitRefused  = 2,   // mismatch, busy, user declined, bad file
    ExitNetwork  = 3,   // discovery / MQTT / test failed
    ExitFlash    = 4,   // printer reported failure or timeout
};

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

volatile std::sig_atomic_t g_interrupted = 0;
extern "C" void onSignal(int) { g_interrupted = 1; }

QString stamp() { return QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss  ")); }

bool confirm(const QString& question)
{
    out() << question << " [y/N] " << Qt::flush;
    QTextStream in(stdin);
    const QString answer = in.readLine().trimmed().toLower();
    return answer == QLatin1String("y") || answer == QLatin1String("yes");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("aceflash"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ACEFLASH_VERSION));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral(
        "Flash Anycubic ACE Pro (Gen 1 / Gen 2) firmware over LAN via the printer.\n\n"
        "Commands:\n"
        "  test    Discover printer, connect MQTT, list connected ACE units (read-only)\n"
        "  flash   Run test, then flash --file onto ACE --ace"));
    p.addHelpOption();
    p.addVersionOption();
    p.addPositionalArgument(QStringLiteral("command"), QStringLiteral("test | flash"));

    const QCommandLineOption optIp({QStringLiteral("i"), QStringLiteral("ip")},
        QStringLiteral("Printer IP address (or env ACEFLASH_IP)."), QStringLiteral("ip"));
    const QCommandLineOption optFile({QStringLiteral("f"), QStringLiteral("file")},
        QStringLiteral("Firmware image: ACE_V<ver>_<date>.bin or ACE2_V<ver>_<date>.bin."),
        QStringLiteral("path"));
    const QCommandLineOption optAce({QStringLiteral("a"), QStringLiteral("ace")},
        QStringLiteral("ACE unit index to flash (default 0)."), QStringLiteral("n"),
        QStringLiteral("0"));
    const QCommandLineOption optYes({QStringLiteral("y"), QStringLiteral("yes")},
        QStringLiteral("Do not ask for confirmation before flashing."));
    const QCommandLineOption optHttpPort(QStringLiteral("http-port"),
        QStringLiteral("Fixed local port for the firmware HTTP server (default: random). "
                       "Open it in your firewall: the printer downloads from this PC."),
        QStringLiteral("port"), QStringLiteral("0"));
    const QCommandLineOption optLocalIp(QStringLiteral("local-ip"),
        QStringLiteral("Local IP the printer should download from (default: auto)."),
        QStringLiteral("ip"));
    const QCommandLineOption optTimeout(QStringLiteral("timeout"),
        QStringLiteral("Connection test timeout in seconds (default 15)."),
        QStringLiteral("s"), QStringLiteral("15"));
    const QCommandLineOption optVerbose(QStringLiteral("verbose"),
        QStringLiteral("Print raw HTTP/MQTT traffic to stderr (contains printer credentials)."));
    p.addOptions({optIp, optFile, optAce, optYes, optHttpPort, optLocalIp, optTimeout, optVerbose});
    p.process(app);

    const QStringList args = p.positionalArguments();
    const QString cmd = args.value(0);
    if (args.size() != 1 || (cmd != QLatin1String("test") && cmd != QLatin1String("flash"))) {
        err() << "Usage: aceflash test|flash --ip <printer> [--file <image>]\n"
                 "Run 'aceflash --help' for all options.\n";
        return ExitUsage;
    }

    QString ip = p.value(optIp);
    if (ip.isEmpty()) ip = qEnvironmentVariable("ACEFLASH_IP");
    if (QHostAddress(ip).protocol() != QAbstractSocket::IPv4Protocol) {
        err() << "Missing or invalid --ip (printer IPv4 address).\n";
        return ExitUsage;
    }

    bool ok = false;
    const int aceId = p.value(optAce).toInt(&ok);
    if (!ok || aceId < 0 || aceId > 3) { err() << "--ace must be 0..3\n"; return ExitUsage; }
    const int httpPort = p.value(optHttpPort).toInt(&ok);
    if (!ok || httpPort < 0 || httpPort > 65535) { err() << "Invalid --http-port\n"; return ExitUsage; }
    const int timeoutS = p.value(optTimeout).toInt(&ok);
    if (!ok || timeoutS <= 0) { err() << "Invalid --timeout\n"; return ExitUsage; }

    // Validate the image before touching the network
    FirmwareFile fw;
    if (cmd == QLatin1String("flash")) {
        if (!p.isSet(optFile)) { err() << "flash requires --file <image>\n"; return ExitUsage; }
        fw = FirmwareFile::load(p.value(optFile));
        if (fw.size <= 0) {
            err() << "Cannot read firmware file: " << p.value(optFile) << "\n";
            return ExitRefused;
        }
        if (fw.target != FlashTarget::AceGen1 && fw.target != FlashTarget::AceGen2) {
            err() << "Not an ACE image name: " << fw.name << "\n"
                  << "Expected ACE_V<ver>_<date>.bin (ACE 1 Pro) or ACE2_V<ver>_<date>.bin (ACE 2 Pro).\n"
                  << "The name selects the generation - do not rename images.\n";
            return ExitRefused;
        }
        out() << "Image: " << fw.name << "  (" << fw.size << " bytes, md5 " << fw.md5 << ", "
              << (fw.target == FlashTarget::AceGen2 ? "ACE 2 Pro / Gen 2" : "ACE 1 Pro / Gen 1")
              << ")\n" << Qt::flush;
    }

    aceflash::SessionOptions opts;
    opts.testTimeoutMs = timeoutS * 1000;
    opts.httpPort      = quint16(httpPort);
    opts.localIp       = p.value(optLocalIp);
    FlashSession session(opts);

    QObject::connect(&session, &FlashSession::log, [](const QString& l) {
        out() << stamp() << l << "\n" << Qt::flush;
    });
    if (p.isSet(optVerbose)) {
        QObject::connect(&session, &FlashSession::trace, [](const QString& l) {
            err() << stamp() << l << "\n" << Qt::flush;
        });
    }

    int lastPct = -1;
    QObject::connect(&session, &FlashSession::flashProgress, [&lastPct](int pct, const QString& st) {
        if (pct == lastPct) return;
        lastPct = pct;
        out() << stamp() << "Progress " << pct << "%  (" << st << ")\n" << Qt::flush;
    });

    QObject::connect(&session, &FlashSession::flashFinished,
                     [&](bool success, const QString& error) {
        if (success) {
            out() << stamp() << "OK  Flash successful. ACE reboots (~30 s).\n" << Qt::flush;
            app.exit(ExitOk);
        } else {
            err() << stamp() << "ERR Flash failed: " << error << "\n";
            if (session.updateStarted())
                err() << "Recovery: reflash the original image from originalFirmware/.\n";
            err() << Qt::flush;
            app.exit(ExitFlash);
        }
        session.disconnectFromPrinter();
    });

    bool confirmed = false;
    QObject::connect(&session, &FlashSession::testFinished,
                     [&](bool success, const QString& error) {
        if (!success) {
            err() << stamp() << "ERR Test failed: " << error << "\n"
                  << "Check: printer powered on, LAN mode enabled, same network.\n" << Qt::flush;
            app.exit(ExitNetwork);
            return;
        }
        out() << stamp() << "Test OK  (" << session.credentials().modelName << ", "
              << (session.printerFree() ? "idle" : "BUSY") << ")\n" << Qt::flush;

        if (cmd == QLatin1String("test")) {
            session.disconnectFromPrinter();
            app.exit(ExitOk);
            return;
        }

        // Run every guard now so the user is never asked about a flash that would be refused
        const QString blocked = aceflash::flashPrecheck(
            session.credentials().stack(), fw.target, aceId, session.aceUnits(),
            session.printerFree());
        if (!blocked.isEmpty()) {
            err() << stamp() << "ERR " << blocked << "\n" << Qt::flush;
            session.disconnectFromPrinter();
            app.exit(ExitRefused);
            return;
        }

        if (!p.isSet(optYes) && !confirmed) {
            const QString q = QStringLiteral("Flash %1 onto ACE%2 (%3)?")
                .arg(fw.name).arg(aceId)
                .arg(aceflash::aceModelName(session.aceModelIds().value(aceId, 0)));
            // Prompt blocks the event loop: drop the MQTT session while waiting,
            // then re-test so busy state / connection are fresh when flashing.
            session.disconnectFromPrinter();
            if (!confirm(q)) {
                out() << "Aborted.\n";
                app.exit(ExitRefused);
                return;
            }
            confirmed = true;
            out() << stamp() << "Re-checking printer before flash ...\n" << Qt::flush;
            session.startTest(ip);
            return;
        }

        QString why;
        if (!session.startFlash(fw, aceId, &why)) {
            err() << stamp() << "ERR " << why << "\n" << Qt::flush;
            session.disconnectFromPrinter();
            app.exit(ExitRefused);
        }
    });

    // Ctrl-C / SIGTERM: stop serving and explain state instead of dying silently
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    QTimer sigPoll;
    QObject::connect(&sigPoll, &QTimer::timeout, [&]() {
        if (!g_interrupted) return;
        sigPoll.stop();
        const bool flashing = session.state() == FlashSession::State::Flashing;
        session.disconnectFromPrinter();
        err() << "\n" << stamp() << "Interrupted."
              << (flashing ? " If the ACE does not come back, reflash it - the bootloader "
                             "is untouched by OTA (see originalFirmware/)."
                           : "")
              << "\n" << Qt::flush;
        app.exit(130);
    });
    sigPoll.start(200);

    QTimer::singleShot(0, &session, [&]() { session.startTest(ip); });
    return app.exec();
}
