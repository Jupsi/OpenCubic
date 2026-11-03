#include "firmware_packager.h"
#include <QRandomGenerator>
#include <QDateTime>
#include <string.h>

// ── Printer stack detection ───────────────────────────────────────────────────

PrinterStack printer_stack_from_model_id(const QString& modelId)
{
    int id = modelId.toInt();
    switch (id) {
    case 20024: // Unknown but existing
    case 20026: // Kobra 3
    case 20025: // Kobra S1
    case 20027: // Kobra 3 Max?
    case 20029: // Kobra S1 Max
    case 20031: // Kobra S1 V2
    case 20032: // Kobra 3 Max V2
        return PrinterStack::KlipperGo;
    case 20030: // Kobra X
        return PrinterStack::Avata;
    default:
        return PrinterStack::Unknown;
    }
}

// ── CRC32 (IEEE 802.3) ────────────────────────────────────────────────────────

static uint32_t g_crc32_table[256];
static bool     g_crc32_ready = false;

static void init_crc32()
{
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc32_table[i] = c;
    }
    g_crc32_ready = true;
}

static uint32_t crc32_buf(const uint8_t* data, int len, uint32_t init = 0xFFFFFFFFu)
{
    if (!g_crc32_ready) init_crc32();
    uint32_t crc = init;
    for (int i = 0; i < len; ++i)
        crc = g_crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static uint32_t crc32_final(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

// ── ZipCrypto (PKWARE legacy encryption) ─────────────────────────────────────

struct ZipKeys { uint32_t k0, k1, k2; };

static ZipKeys zip_keys_init(const QString& password)
{
    if (!g_crc32_ready) init_crc32();
    ZipKeys k = { 305419896u, 591751049u, 878082192u };
    const QByteArray pw = password.toLatin1();
    for (unsigned char c : pw) {
        k.k0 = (g_crc32_table[(k.k0 ^ c) & 0xFF] ^ (k.k0 >> 8));
        k.k1 = (k.k1 + (k.k0 & 0xFF)) * 134775813u + 1u;
        k.k2 = (g_crc32_table[(k.k2 ^ (k.k1 >> 24)) & 0xFF] ^ (k.k2 >> 8));
    }
    return k;
}

static uint8_t zip_encrypt(ZipKeys& k, uint8_t plain)
{
    uint16_t t = static_cast<uint16_t>(k.k2 | 2u);
    uint8_t  c = static_cast<uint8_t>((t * (t ^ 1u)) >> 8) ^ plain;
    // update keys with plaintext
    k.k0 = (g_crc32_table[(k.k0 ^ plain) & 0xFF] ^ (k.k0 >> 8));
    k.k1 = (k.k1 + (k.k0 & 0xFF)) * 134775813u + 1u;
    k.k2 = (g_crc32_table[(k.k2 ^ (k.k1 >> 24)) & 0xFF] ^ (k.k2 >> 8));
    return c;
}

static QByteArray zip_encrypt_buf(const QByteArray& plain, const QString& password,
                                   uint32_t crc32_of_plain)
{
    ZipKeys k = zip_keys_init(password);
    QByteArray out;
    out.reserve(12 + plain.size());

    // 12-byte encryption header: 11 random bytes + (CRC >> 24)
    uint8_t ehdr[12];
    for (int i = 0; i < 11; ++i)
        ehdr[i] = static_cast<uint8_t>(QRandomGenerator::global()->bounded(256));
    ehdr[11] = static_cast<uint8_t>((crc32_of_plain >> 24) & 0xFF);

    for (int i = 0; i < 12; ++i)
        out.append(static_cast<char>(zip_encrypt(k, ehdr[i])));
    for (unsigned char b : plain)
        out.append(static_cast<char>(zip_encrypt(k, b)));

    return out;
}

// ── ZIP binary format helpers ─────────────────────────────────────────────────

static void u16le(QByteArray& b, uint16_t v)
{
    b.append(char(v & 0xFF));
    b.append(char((v >> 8) & 0xFF));
}
static void u32le(QByteArray& b, uint32_t v)
{
    b.append(char(v & 0xFF));
    b.append(char((v >> 8) & 0xFF));
    b.append(char((v >> 16) & 0xFF));
    b.append(char((v >> 24) & 0xFF));
}

// DOS timestamp for current date, 00:00:00
static void dos_datetime(QByteArray& b)
{
    QDate d = QDate::currentDate();
    uint16_t time = 0;
    uint16_t date = static_cast<uint16_t>(
        ((d.year() - 1980) << 9) | (d.month() << 5) | d.day());
    u16le(b, time);
    u16le(b, date);
}

// ── ZIP with ZipCrypto (STORE, single entry) ──────────────────────────────────

QByteArray FirmwarePackager::makeEncryptedZip(const QByteArray& data,
                                               const QString&    entryPath,
                                               const QString&    password)
{
    const QByteArray fname     = entryPath.toUtf8();
    const uint16_t   fname_len = static_cast<uint16_t>(fname.size());

    uint32_t data_crc   = crc32_final(crc32_buf(
        reinterpret_cast<const uint8_t*>(data.constData()), data.size()));
    QByteArray enc_data = zip_encrypt_buf(data, password, data_crc);
    uint32_t comp_size  = static_cast<uint32_t>(enc_data.size());   // = data.size() + 12
    uint32_t uncomp_sz  = static_cast<uint32_t>(data.size());

    // ── Local File Header ───────────────────────────────────────────────────
    QByteArray lfh;
    u32le(lfh, 0x04034B50u);  // signature
    u16le(lfh, 20);            // version needed (2.0)
    u16le(lfh, 0x0001u);       // flags: encrypted
    u16le(lfh, 0);             // compression: STORE
    dos_datetime(lfh);
    u32le(lfh, data_crc);
    u32le(lfh, comp_size);
    u32le(lfh, uncomp_sz);
    u16le(lfh, fname_len);
    u16le(lfh, 0);             // extra field length
    lfh.append(fname);

    // ── Central Directory Entry ─────────────────────────────────────────────
    uint32_t lfh_offset = 0;   // Local header starts at byte 0
    QByteArray cde;
    u32le(cde, 0x02014B50u);  // signature
    u16le(cde, 0x031Eu);       // version made by (3.0 = Unix)
    u16le(cde, 20);            // version needed
    u16le(cde, 0x0001u);       // flags: encrypted
    u16le(cde, 0);             // compression: STORE
    dos_datetime(cde);
    u32le(cde, data_crc);
    u32le(cde, comp_size);
    u32le(cde, uncomp_sz);
    u16le(cde, fname_len);
    u16le(cde, 0);             // extra
    u16le(cde, 0);             // comment
    u16le(cde, 0);             // disk start
    u16le(cde, 0);             // internal attrs
    u32le(cde, 0);             // external attrs
    u32le(cde, lfh_offset);
    cde.append(fname);

    // ── End of Central Directory ────────────────────────────────────────────
    uint32_t cd_offset = static_cast<uint32_t>(lfh.size() + enc_data.size());
    uint32_t cd_size   = static_cast<uint32_t>(cde.size());
    QByteArray eocd;
    u32le(eocd, 0x06054B50u);
    u16le(eocd, 0);             // disk number
    u16le(eocd, 0);             // disk with CD start
    u16le(eocd, 1);             // entries on disk
    u16le(eocd, 1);             // total entries
    u32le(eocd, cd_size);
    u32le(eocd, cd_offset);
    u16le(eocd, 0);             // comment length

    QByteArray zip;
    zip.reserve(lfh.size() + enc_data.size() + cde.size() + eocd.size());
    zip.append(lfh);
    zip.append(enc_data);
    zip.append(cde);
    zip.append(eocd);
    return zip;
}

// ── ustar TAR (single entry) ──────────────────────────────────────────────────

QByteArray FirmwarePackager::makeTar(const QByteArray& data, const QString& name)
{
    const QByteArray fname = name.toUtf8().left(99);  // ustar: max 100 bytes incl. \0

    // 512-byte header block (zero-initialized)
    uint8_t hdr[512] = {};

    // Filename [0..99]
    memcpy(hdr, fname.constData(), static_cast<size_t>(fname.size()));

    // Mode [100..107]
    memcpy(hdr + 100, "0000644", 7);

    // UID/GID [108..123]
    memcpy(hdr + 108, "0000000", 7);
    memcpy(hdr + 116, "0000000", 7);

    // File size [124..135] — octal, null-terminated
    qsnprintf(reinterpret_cast<char*>(hdr + 124), 12, "%011o",
              static_cast<unsigned>(data.size()));

    // mtime [136..147] — Unix timestamp, octal
    qsnprintf(reinterpret_cast<char*>(hdr + 136), 12, "%011llo",
              static_cast<unsigned long long>(QDateTime::currentSecsSinceEpoch()));

    // Type flag [156] — '0' = regular file
    hdr[156] = '0';

    // ustar indicator [257..264]
    memcpy(hdr + 257, "ustar  ", 8);  // GNU-tar variant

    // Calculate checksum (checksum field = 8 spaces)
    memset(hdr + 148, ' ', 8);
    uint32_t chk = 0;
    for (int i = 0; i < 512; ++i) chk += hdr[i];
    qsnprintf(reinterpret_cast<char*>(hdr + 148), 8, "%06o", chk);
    hdr[154] = '\0';
    hdr[155] = ' ';

    // Pad file data to 512-byte block boundary
    int pad = (512 - (data.size() % 512)) % 512;

    QByteArray tar;
    tar.reserve(512 + data.size() + pad + 1024);
    tar.append(reinterpret_cast<const char*>(hdr), 512);
    tar.append(data);
    if (pad) tar.append(QByteArray(pad, '\0'));
    tar.append(QByteArray(1024, '\0'));  // two end-of-archive blocks
    return tar;
}

// ── Stack-specific packaging ──────────────────────────────────────────────────

// Password from ota.go: const unzippd = "U2FsdGVkX19deTfqpXHZnB5GeyQ/dtlbHjkUnwgCi+w="
static const QString KLIPPER_ZIP_PASSWORD =
    QStringLiteral("U2FsdGVkX19deTfqpXHZnB5GeyQ/dtlbHjkUnwgCi+w=");

QByteArray FirmwarePackager::packageKlipperGo(const QByteArray& binData,
                                               const QString&    binName)
{
    // TAR containing ACE_V*.bin
    QByteArray tar = makeTar(binData, binName);

    // ZIP: update_swu/setup.tar  (path that klipper-go expects after extraction)
    return makeEncryptedZip(tar, QStringLiteral("update_swu/setup.tar"),
                            KLIPPER_ZIP_PASSWORD);
}

QByteArray FirmwarePackager::packageAvata(const QByteArray& binData)
{
    // avata (workflow.cpp): if firmware_name does NOT end in ".swu",
    // the file is used directly as a binary (safe_read_file -> ace_upgrade).
    // Verified in avata_main/develop/klippy/anycubic/workflow.cpp:1840
    return binData;
}

QByteArray FirmwarePackager::package(const QByteArray& binData,
                                      const QString&    binName,
                                      PrinterStack      stack,
                                      QString&          outServeName)
{
    switch (stack) {
    case PrinterStack::KlipperGo:
        // Container name matches the binary name; printer does not detect .zip by name
        outServeName = binName;
        return packageKlipperGo(binData, binName);

    case PrinterStack::Avata:
    case PrinterStack::Unknown:
    default:
        outServeName = binName;
        return packageAvata(binData);
    }
}
