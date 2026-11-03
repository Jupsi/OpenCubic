#pragma once
#include <QByteArray>
#include <QString>

// Printer stack determines the container format
enum class PrinterStack {
    KlipperGo,  // Kobra 3, Kobra 3 Max, Kobra 3 V2, Kobra S1, Kobra S1 Max, Kobra S1 V2, Kobra 3 Max V2
                // — modeId 20024/20025/20026/20027/20029/20031/20032
    Avata,      // Kobra X — modeId 20030
    Unknown,
};

PrinterStack printer_stack_from_model_id(const QString& modelId);

class FirmwarePackager {
public:
    // Builds the serveable container from the raw .bin.
    // outServeName: filename used in the HTTP/MQTT payload.
    // Returns an empty QByteArray on failure.
    static QByteArray package(const QByteArray& binData,
                               const QString&    binName,
                               PrinterStack      stack,
                               QString&          outServeName);

private:
    // klipper-go: ZIP(PW) -> update_swu/setup.tar -> binName
    static QByteArray packageKlipperGo(const QByteArray& binData,
                                        const QString&    binName);

    // avata: raw binary (format to be verified with a real device)
    static QByteArray packageAvata(const QByteArray& binData);

    // Minimal ustar TAR with a single file
    static QByteArray makeTar(const QByteArray& data, const QString& name);

    // ZIP with ZipCrypto encryption (STORE), single entry
    static QByteArray makeEncryptedZip(const QByteArray& data,
                                        const QString&    entryPath,
                                        const QString&    password);
};
