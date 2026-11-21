# aceflash — Linux CLI flash tool

`aceflash` is a headless command-line build of the ACE Flash Tool. It uses the same core
(discovery, MQTT, packaging, OTA) and the same safety checks as the Windows GUI, and runs on
Linux (and should run on macOS / Windows, where it is untested).

```
aceflash test  --ip 192.168.1.50
aceflash flash --ip 192.168.1.50 --file ACE_V1.3.863_20261103.bin
```

## Tested hardware

| Printer | Printer FW | ACE | Result |
|---|---|---|---|
| Kobra S1 (KlipperGo) | 2.7.2.7 | ACE 1 Pro (Gen 1) | CLI `test` + `flash` CFW v1.1.2 OK; GUI Test Connection + flash OK (Debian, Ubuntu, Arch builds) |

**Not tested on hardware:** ACE 2 Pro (Gen 2), the AVATA stack (Kobra X), Kobra S1 Max / K3 Max /
other KlipperGo models, multiple ACE units. Those paths share code with the GUI and are covered by
unit tests only. Reports welcome.

The Qt GUI (`AceFlashTool`) also builds and runs on Linux with `-DBUILD_GUI=ON`; see
[GUI on Linux](#gui-on-linux).

## Build

Requirements: CMake ≥ 3.20, a C++17 compiler, Qt 6 (or Qt 5.15) Core + Network, OpenSSL (libcrypto).
`unzip` and `tar` are needed only to run the tests.

Arch / CachyOS:

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base openssl unzip
```

Debian 13 (trixie) / Ubuntu 24.04+:

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev libssl-dev unzip
```

Clone with submodules (qmqtt) and build:

```bash
git clone --recurse-submodules https://github.com/Jupsi/OpenCubic.git
cmake -S OpenCubic/flashtool -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build        # optional: installs aceflash to /usr/local/bin
```

CMake options: `BUILD_CLI` (ON), `BUILD_GUI` (OFF on Linux), `BUILD_TESTING` (ON),
`ACEFLASH_QT_MAJOR` (`6` default on Linux, set `5` to force Qt 5).

### GUI on Linux

```bash
cmake -S OpenCubic/flashtool -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=ON
cmake --build build && ./build/AceFlashTool
```

On Debian/Ubuntu also install `qt6-qpa-plugins` (the X11/xcb platform plugin) if the window does
not open; on a Wayland session without `qt6-wayland` Qt uses XWayland. The GUI picks a random HTTP
port; behind a firewall start it with a fixed one (same meaning as the CLI's `--http-port` /
`--local-ip`):

```bash
ACEFLASH_HTTP_PORT=8765 ./build/AceFlashTool
```

`ACEFLASH_LOCAL_IP` overrides the address the printer downloads from (needed in containers).

Without installing anything on the host, build and test in Docker with
[`ci/compose.yaml`](../ci/compose.yaml) (run from the repo root):

```bash
docker compose -f ci/compose.yaml run --rm test          # Debian trixie (DEBIAN=bookworm to switch)
docker compose -f ci/compose.yaml run --rm test-ubuntu   # Ubuntu 24.04 (UBUNTU=<tag> to switch)
docker compose -f ci/compose.yaml run --rm test-arch     # Arch Linux
docker compose -f ci/compose.yaml run --rm gui-debian    # + GUI build, Xvfb run (gui-ubuntu, gui-arch)
```

`test-*` does a clean Debug build of `flashtool/` and runs the unit tests. `gui-*` also builds the
GUI, runs the GUI smoke test offscreen and on Xvfb, starts the app, and saves screenshots to
`ci/shots/<distro>/`; GitHub Actions (`.github/workflows/linux.yml`) runs the `gui-*` check for all
three distros. Hardware tests are opt-in: `tst_gui liveConnectionTest` with
`ACEFLASH_TEST_PRINTER_IP`, and `tst_gui liveFlash` (a **real flash**) additionally with
`ACEFLASH_TEST_FLASH_FILE`. The `aceflash` service runs
the CLI itself from a container:

```bash
docker compose -f ci/compose.yaml run --rm aceflash test --ip 192.168.1.50
```

For `flash` from the container see [Firewall → No root?](#firewall).

## Usage

Enable **LAN mode** on the printer first. The PC and printer must be on the same network.

| Option | Meaning |
|---|---|
| `-i, --ip <ip>` | Printer IPv4 address (or env `ACEFLASH_IP`) |
| `-f, --file <path>` | Firmware image, `flash` only |
| `-a, --ace <n>` | ACE unit 0..3 (default 0) |
| `-y, --yes` | Do not ask before flashing |
| `--http-port <port>` | Fixed port for the local firmware server (default: random) — see [Firewall](#firewall) |
| `--local-ip <ip>` | Address the printer should download from (default: auto) |
| `--timeout <s>` | Connection test timeout (default 15) |
| `--verbose` | Raw HTTP/MQTT traffic on stderr — **contains printer credentials** |

`test` is read-only: it discovers the printer, connects over MQTT and lists the connected ACE units,
their generation and loaded slots.

`flash` runs the same test, then refuses to continue if the printer is busy, the ACE index is not
connected, or the image generation does not match the ACE (soft-brick guard). It asks for
confirmation, re-checks the printer, serves the image and starts the OTA.

The generation is taken from the file name — `ACE_V<ver>_<date>.bin` is Gen 1,
`ACE2_V<ver>_<date>.bin` is Gen 2. **Do not rename images**; renamed files are refused.

Exit codes: `0` ok · `1` usage · `2` refused (mismatch, busy, declined, bad file) ·
`3` network / test failed · `4` flash failed · `130` interrupted.

## Firewall

During `flash` the **printer downloads the image from your PC** over HTTP. An inbound firewall on
the PC blocks this, and the printer then reports `download-failed`. aceflash detects that no
download request arrived and prints the rule to add; the ACE firmware is not touched in that case.

Use a fixed port and allow it from the printer only, for the duration of the flash:

```bash
aceflash flash --ip 192.168.1.50 --file ACE_V1.3.863_20261103.bin --http-port 8765
```

ufw (Arch, Ubuntu):

```bash
sudo ufw allow from 192.168.1.50 to any port 8765 proto tcp
sudo ufw delete allow from 192.168.1.50 to any port 8765 proto tcp   # afterwards
```

firewalld (Fedora, openSUSE, some Debian setups) — runtime rule, gone after reload/reboot:

```bash
sudo firewall-cmd --add-rich-rule='rule family="ipv4" source address="192.168.1.50" port port="8765" protocol="tcp" accept'
```

nftables / iptables: allow inbound TCP 8765 from the printer IP on the LAN interface.

**No sudo, but Docker access?** Ports published by Docker bypass ufw. Run aceflash from the
compose service with its port published (`--service-ports`) and the host's LAN address passed in;
`ACEFLASH_FW_DIR` is mounted at `/fw` (default: `originalFirmware/`):

```bash
ACEFLASH_FW_DIR=~/Downloads docker compose -f ci/compose.yaml run --rm --service-ports \
  aceflash flash --ip 192.168.1.50 --file /fw/ACE_V1.3.863_20261103.bin \
  --http-port 8765 --local-ip <this-pc-lan-ip>
```

`ACEFLASH_HTTP_PORT` changes the published port (keep `--http-port` the same). `docker compose run`
allocates a terminal, so the confirmation prompt works; add `-T --yes` for scripts.

## Troubleshooting

- **`Discovery: GET /info failed`** — wrong IP, printer off, or LAN mode disabled.
- **`Test failed: Timeout`** — MQTT (TCP 9883 on the printer) unreachable; check VLAN / AP isolation.
- **`download-failed`** with the firewall hint — see [Firewall](#firewall).
- **Several network interfaces / VPN** — if the served URL shows the wrong address, pass
  `--local-ip` with the PC's address on the printer's network.
- **Flash failed after `updating`** — the ACE may not boot. The bootloader is never overwritten by
  OTA: re-flash the original image from [`originalFirmware/`](../originalFirmware/). See
  [how-to-flash.md](how-to-flash.md#recovery).
