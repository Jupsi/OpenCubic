# OpenCubic — ACE Custom Firmware (Gen 1 + Gen 2)

Custom firmware (CFW) for the Anycubic **ACE 1 Pro** (Gen 1) and **ACE 2 Pro** (Gen 2) filament
management systems (GD32F303, FreeRTOS).

> **Download the latest firmware:** See [Releases](https://github.com/Jupsi/OpenCubic/releases)  
> Latest: **ACE 2 Pro CFW v1.0.6** · **ACE 1 Pro CFW v1.1.1** · **ACE Flash Tool v1.0.3**

---

## ⚠️ Warnings

### Use the firmware that matches your ACE generation

Each generation has its own firmware:

- **ACE 1 Pro (Gen 1):** `ACE_V…_….bin` — KlipperGo-stack printers (e.g. Kobra 3, Kobra S1)
- **ACE 2 Pro (Gen 2):** `ACE2_V…_….bin` — AVATA-stack printers (e.g. Kobra X) and KlipperGo-stack
  printers with ACE 2 Pro support (e.g. Kobra S1 Max)

Flashing the **wrong generation's** firmware onto an ACE **will soft-brick the device**. The Flash
Tool blocks obvious mismatches (a `ACE_V…` Gen 1 image on an AVATA printer, or — when the printer
reports the model of the connected ACE — an image that does not match that ACE), but this is a
safeguard — **it remains your responsibility to check which ACE you own and pick the matching `.bin`.**

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

#### Building on macOS / Linux (community-supported)

Official releases are **Windows only**. The Flash Tool also builds on macOS and Linux, but these
builds are not tested by the maintainer — use at your own risk. macOS support was contributed by
[@ozgunawesome](https://github.com/ozgunawesome) ([#6](https://github.com/Jupsi/OpenCubic/pull/6)).

The MQTT client is a git submodule — clone with submodules first (on every platform):

```bash
git clone --recurse-submodules https://github.com/Jupsi/OpenCubic.git
# or, in an existing clone:
git submodule update --init
```

**macOS** (Apple Silicon or Intel, [Homebrew](https://brew.sh)):

```bash
brew install cmake qt@5
cd flashtool
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt@5)"
cmake --build build
open build/AceFlashTool.app
```

- The app is signed **ad-hoc** by default (required on Apple Silicon, no Apple account needed).
  To sign with your own identity: `-DACEFLASHTOOL_CODESIGN_IDENTITY="Apple Development: …"`.
- On first start, allow **Local Network** access when macOS asks — otherwise the printer can't be found.
- The app links against your Homebrew Qt. To get a self-contained `.app`, run
  `"$(brew --prefix qt@5)/bin/macdeployqt" build/AceFlashTool.app` and re-sign it.

**Linux** (Debian/Ubuntu example):

```bash
sudo apt install build-essential cmake qtbase5-dev libssl-dev
cd flashtool
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/AceFlashTool
```

On all platforms the printer must be in **LAN mode** (the discovery port 18910 is closed in cloud mode),
and your firewall must allow incoming connections — the printer downloads the firmware from the tool's
built-in HTTP server.

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
