# Changelog — OpenCubic

Public release notes for the **ACE Flash Tool** and the **Custom Firmware (CFW)** for Anycubic ACE
filament systems (ACE 1 Pro and ACE 2 Pro).

---

## ACE Flash Tool

### v1.0.3 — 2026-11-03

#### Fixed
- Flashing the **Kobra S1 Max** failed with `update-failed` (code `10802`) right after the
  download ([#1](https://github.com/Jupsi/OpenCubic/issues/1)): the printer (model id `20029`)
  was not recognized, so the tool sent the raw `.bin` instead of the KlipperGo OTA container
  the printer expects. `20029` is now recognized as a KlipperGo-stack printer.
- **Kobra S1 V2** (`20031`) was wrongly treated as an AVATA-stack printer and **Kobra 3 Max V2**
  (`20032`) was not recognized at all. Both are now recognized as KlipperGo-stack printers.

#### Changed
- **ACE 2 Pro on KlipperGo-stack printers:** newer KlipperGo printer firmware (e.g. Kobra S1 Max)
  supports the ACE 2 Pro, using the same OTA container for `ACE_V…` and `ACE2_V…` images.
  The v1.0.1 rule "KlipperGo stack + `ACE2_V…` image → blocked" has been removed; the mismatch guard
  is now:
  - AVATA stack + `ACE_V…` (Gen 1) image → blocked (unchanged)
  - KlipperGo stack → `ACE_V…` and `ACE2_V…` images **allowed**
  - If the printer reports the model of the connected ACE (`model_id`, newer printer firmware),
    the image must match the selected ACE (`ACE_V…` ↔ ACE 1 Pro, `ACE2_V…` ↔ ACE 2 Pro);
    otherwise flashing is blocked and the reason is shown in the log.
- Test Connection now shows the model of each connected ACE (e.g. `model=ACE 2 Pro`) when the
  printer reports it.

### v1.0.2 — 2026-08-17

#### Fixed
- Flashing failed with `update-failed` on printers running newer firmware (Kobra S1 after 2.7.x):
  since 2.7 the printer expects a `model_id` field in the OTA request and rejects requests
  without it. The tool now always sends the matching id (`40001` = ACE 1 Pro, `40002` = ACE 2 Pro,
  derived from the selected firmware image). Firmware 2.6.x is unaffected.

### v1.0.1 — 2026-05-29

#### Added
- **ACE 2 Pro support** (AVATA-stack printers, e.g. Kobra X): flashing the Gen 2 CFW is now enabled.
- The previous blanket AVATA block was replaced by a firmware/printer **mismatch guard**:
  - AVATA stack + `ACE2_V…` image → **allowed**
  - AVATA stack + `ACE_V…` (Gen 1) image → blocked (soft-brick protection)
  - KlipperGo stack + `ACE_V…` image → **allowed**
  - KlipperGo stack + `ACE2_V…` image → blocked (soft-brick protection)

### v1.0 — 2026-05-22

- Initial release: LAN/MQTT printer discovery + OTA flashing of the CFW onto **ACE Gen 1**
  (KlipperGo-stack printers, e.g. Kobra 3 / Kobra S1).

---

## CFW — ACE 2 Pro

### v1.0.6 — 2026-08-17

> Based on official stock firmware **V1.1.31**. Reports version `V1.0.0` to the printer
> (kept intentionally low so cloud mode still offers the official firmware as a recovery image).

#### Added
- Debug firmware variant with NFC traces on the debug serial port (raw tag hex dump, per-parser
  match trace) — same diagnostics as the ACE 1 Pro debug build, useful when reporting
  unrecognized spools.

#### Fixed
- All NFC fixes from the ACE 1 Pro CFW v1.1 and v1.1.1 releases are now included
  (see the ACE 1 Pro section below for details):
  - Third-party NTAG spools (TigerTag, OpenSpool, …) are read without authentication first —
    previously the Anycubic-password attempt silenced unprotected foreign tags and they were
    never recognized.
  - TigerTag parser rewritten against the official TigerTag spec (verified on the ACE 1 Pro
    with a real tag).
  - OpenSpool spec compliance: adaptive read size for larger tags, color without `#` prefix,
    temperatures in string form (verified on the ACE 1 Pro with a real tag).
- Firmware updates could fail with a CRC verification error when the image grew beyond the
  update slot; the build now enforces the limit up front.

> Note: these fixes are hardware-verified on the ACE 1 Pro; on the ACE 2 Pro the shared NFC
> layer is identical but re-testing on real hardware is still pending. Anycubic and Bambu Lab
> detection is unaffected by design (separate read paths).

### v1.0.4 — 2026-05-29

> First CFW for the **ACE 2 Pro**, based on official stock firmware **V1.1.31**.
> Reports version `V1.0.0` to the printer (kept intentionally low so cloud mode still offers the official
> firmware as an update — useful as a recovery image).

#### Added
- Multi-vendor NFC framework (ported from the ACE 1 Pro CFW): **Anycubic, Bambu Lab**, OpenPrintTag,
  OpenSpool, Elegoo, TigerTag. QIDI/Creality are stubs (keys unknown).
- Bambu Lab MIFARE Classic support (HKDF-SHA256 sector key derivation, Crypto1 authentication).
- RxGain increased to 48 dB (+15 dB vs. default 33 dB) for all NFC reader slots
- Integrated as an **additive layer**: the stock Anycubic read path is unchanged; foreign tags are
  handled via a fallback → zero regression for Anycubic spools.

#### Confirmed
- Anycubic NTAG and Bambu Lab MIFARE recognized reliably and stably (material/color/temperature correct).
- The other vendors (OpenPrintTag/OpenSpool/Elegoo/TigerTag) are ported but not yet tested on a real tag.

#### Fixed
- Initial stack pointer used `0x800` instead of `_estack` → the bootloader never jumped to the app.
- C++ `new`/`delete` and `rand()` routed to the FreeRTOS heap.
- IAP signature trailer appended to the image (required by the running firmware to apply an OTA update).
- Stack sizing for the HKDF-SHA256 path; MIFARE re-select before authentication (Bambu read fix).

---

## CFW — ACE 1 Pro

### v1.1.1 — 2026-08-17

> Based on official Anycubic ACE firmware **v1.3.863**

#### Fixed
- **OpenSpool tags are now read reliably** (verified on real hardware with a community
  198-byte OpenSpool tag):
  - The reader previously fetched only the first 176 bytes of a tag. OpenSpool tags that carry
    optional fields (bed temperatures, spool id, …) exceed that, were treated as truncated and
    the spool never appeared. The read now adapts to the tag's declared memory size — the full
    NTAG215 range is covered (OpenSpool supports NTAG215/216).
  - Color is parsed as the OpenSpool spec defines it — `color_hex` **without** a `#` prefix.
    Tags written with a leading `#` keep working.
  - Temperatures are accepted in the spec's string form (`"min_temp": "220"`) as well as as
    plain numbers. Bed temperatures are picked up too, when present.
- Anycubic, Bambu Lab and TigerTag spools are unaffected by the larger read window
  (unchanged read paths / window only grows for tags that declare more memory).

### v1.1 — 2026-07-23

> Based on official Anycubic ACE firmware **v1.3.863**

#### Added
- **TigerTag support — now verified on real hardware**: parser rewritten against the official
  TigerTag spec (TigerTag-RFID-Guide): correct magic values, big-endian field layout, material/brand
  ID lookup. Material, color, weight, nozzle/bed temperatures and drying parameters all decode
  correctly (verified with a Jayo PLA+ Maker tag).
- Debug firmware variant with NFC traces on a second USB serial port (raw tag hex dump, per-parser
  match trace) — useful when reporting unrecognized spools.

#### Fixed
- Third-party NTAG spools (TigerTag, OpenSpool, …) were unreadable: the reader authenticated with
  the Anycubic password first, which silences unprotected foreign tags (NAK → IDLE state). Tags are
  now read without authentication first; the Anycubic passwords are only tried as a fallback. This
  also stops hammering foreign tags with wrong passwords (AUTHLIM lockout risk).
  Anycubic Gen 1/Gen 2 and Bambu Lab spools verified unaffected.
- NTAG password-auth timeout raised 2 ms → 10 ms (same reader-timing fix as block reads in v1.0)
- Firmware updates could fail with a CRC verification error when the image grew beyond the 112 KB
  update slot; the build now enforces this limit at link time.

### v1.0 — 2026-05-22

> Based on official Anycubic ACE firmware **v1.3.863**

#### Added
- Multi-vendor NFC framework: Anycubic Gen 1/Gen 2, Bambu Lab, Elegoo, TigerTag, OpenSpool, OpenPrintTag
- Bambu Lab MIFARE Classic support (HKDF-SHA256 sector key derivation)
- SKU propagation for all NFC parsers — slicer edit lock active (rfid=2) for all recognized spools
- RxGain increased to 48 dB (+15 dB vs. default 33 dB) for all NFC reader slots
- Case-start immediate read: no full spool rotation needed when chip is already at the reader on insertion

#### Fixed
- NFC color byte order: Anycubic and Bambu colors now display correctly in the slicer
- Hub-tag stop strategy: Bambu spools (hub-mounted tag) use a soft stop at 5 mm/s to reduce inertia overshoot after motor stop
- Partial MIFARE read resilience: sector 2 auth failure no longer discards valid data from sectors 0+1
- GCC -O2 SPI delay: `volatile` keyword added to bitbang SPI delay loops (NFC was completely inactive without this)
- NFC timer: increased from 1 ms to 10 ms for both NTAG and MIFARE block reads/auth (response time ~1.84 ms at 106 kbps)
- Boot crash on GCC: static C++ objects with virtual destructors replaced with heap allocation to avoid `__cxa_guard_acquire` (LDREX/STREX) crash on FreeRTOS startup
