### This fork of OpenCubic has macOS support and a force flashing option.

If you're using an **ACE 2 Pro** (Gen 2) with a **Kobra S1** the flasher in the upstream repository won't let you.

A force override option has been added (I needed it for the case above) which will bypass the Gen 1 / Gen 2 check, but I don't know what would happen if you flash the wrong firmware. (Stuck in bootloader or brick probably.)

Flashing in macOS is the same as flashing in Linux more or less, install Homebrew, then Qt and all other build dependencies.

You'll need to codesign the app package. You can get 1-week valid certificates for free with any iCloud account.

Set everything up from Xcode and build as if you're building for Linux. (Add Qt and other dependencies to your path)

# OpenCubic — ACE Custom Firmware (Gen 1 + Gen 2)

Custom firmware (CFW) for the Anycubic **ACE 1 Pro** (Gen 1) and **ACE 2 Pro** (Gen 2) filament
management systems (GD32F303, FreeRTOS).

> **Download the latest firmware:** See [Releases](https://github.com/Jupsi/OpenCubic/releases)  
> Latest: **ACE 2 Pro CFW v1.0.4** · **ACE 1 Pro CFW v1.0** · **ACE Flash Tool v1.0.1**

---

## ⚠️ Warnings

### Use the firmware that matches your ACE generation

Each generation has its own firmware:

- **ACE 1 Pro (Gen 1):** `ACE_V…_….bin` — KlipperGo-stack printers (e.g. Kobra 3, Kobra S1)
- **ACE 2 Pro (Gen 2):** `ACE2_V…_….bin` — AVATA-stack printers (e.g. Kobra X)

Flashing the **wrong generation's** firmware onto an ACE **will soft-brick the device**. The Flash
Tool blocks obvious mismatches (a `ACE_V…` Gen 1 image on an AVATA printer, or an `ACE2_V…` Gen 2
image on a KlipperGo printer), but this is a safeguard — **it remains your responsibility to check
which ACE you own and pick the matching `.bin`.**

> Recovery without a hardware flash programmer is normally possible: an OTA flash never overwrites the
> bootloader, so a unit that fails to boot falls back to update mode and can be re-flashed over the
> network. See [docs/how-to-flash.md](docs/how-to-flash.md#recovery).

### Use at your own risk

Flashing custom firmware modifies your device. **I take no responsibility for any damage, data loss,
or malfunction** resulting from using this firmware or flash tool. You flash at your own risk.

### Restoring original firmware

The original Anycubic ACE firmware binaries are stored in [`originalFirmware/`](originalFirmware/)
(Gen 1 and Gen 2). You can restore them at any time using the same flash tool.

---

## Features

- **Multi-vendor NFC** — reads filament spools from multiple brands automatically (same on both generations)
- **Improved NFC reliability** — maximum RX gain + TX power, chip-type-aware reads

| ACE with Bambu & Anycubic spools | Slicer showing recognized spools |
|:---:|:---:|
| ![ACE Loaded](images/ACE_Loaded.jpg) | ![Slicer Loaded](images/Slicer_Loaded.jpg) |

### Supported NFC Spools

Full details: [docs/supported-spools.md](docs/supported-spools.md)

| Brand | Status |
|---|---|
| Anycubic Gen 1 | ✅ Confirmed |
| Anycubic Gen 2 | ✅ Confirmed |
| Bambu Lab | ✅ Confirmed |
| Elegoo | ✅ Implemented / untested |
| TigerTag | ✅ Confirmed |
| OpenSpool | ✅ Confirmed |
| OpenPrintTag (Prusa) | ✅ Implemented / untested |
| QIDI | 🔄 Unknown AES keys |
| Creality | 🔄 Unknown AES keys |

---

## Flash Tool

This repo contains the **ACE Flash Tool** — a Qt desktop application that flashes the firmware over
your local network (MQTT via your printer), for both ACE generations.

→ See [docs/how-to-flash.md](docs/how-to-flash.md)

### Build the Flash Tool

Requirements: Qt 5.15+, CMake 3.20+, MSVC 2019/2022

```bash
cd flashtool
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The CMakeLists.txt defaults to `E:/Qt/5.15.2/msvc2019_64` for Qt and
`E:/Qt/Tools/QtCreator/bin` for the OpenSSL DLLs. Override if your paths differ:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="C:/Qt/5.15.2/msvc2019_64" \
  -DOPENSSL_SRC="C:/Qt/Tools/QtCreator/bin"
```

---

## Firmware Source

The firmware source code is maintained privately. Compiled release binaries are provided here.

Some parts of the underlying platform and communication protocols are proprietary and
undocumented. The source is therefore not publicly distributed at this time.

---

## Support

If you find this project useful and want to help cover the cost of filament spools from
more brands for NFC testing and implementation — donations are always welcome, never expected.

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/jupsi)

---

## License

Flash tool: MIT  
Firmware binaries: provided as-is, use at your own risk
