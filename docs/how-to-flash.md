# How to Flash the ACE CFW

Works for both **ACE 1 Pro** (Gen 1) and **ACE 2 Pro** (Gen 2). The Flash Tool auto-detects the
printer and the firmware generation.

## Requirements

- An Anycubic ACE connected to a supported printer:
  - **ACE 1 Pro (Gen 1)** — KlipperGo-stack printers (e.g. Kobra 3, Kobra S1)
  - **ACE 2 Pro (Gen 2)** — AVATA-stack printers (e.g. Kobra X) and KlipperGo-stack printers with
    ACE 2 Pro support (e.g. Kobra S1 Max)
- Printer must be in **LAN mode** (not connected to the Anycubic cloud)
- Printer and PC on the same local network
- ACE Flash Tool — download the pre-built Windows binary from
  [Releases](https://github.com/Jupsi/OpenCubic/releases), or
  [build from source](../README.md#build-the-flash-tool) (Qt 5.15+, CMake 3.20+, MSVC)

## Steps

1. Download the firmware `.bin` for **your ACE generation** from [Releases](https://github.com/Jupsi/OpenCubic/releases):
   - ACE 1 Pro → `ACE_V…_….bin`
   - ACE 2 Pro → `ACE2_V…_….bin`
2. Open `AceFlashTool.exe`
3. Enter your printer's IP address
4. Click **Test Connection** and wait until the log shows `Test OK - Connection remains active`
5. Select the `.bin` file (matching your ACE generation)
6. Click **Flash**

The ACE will reboot automatically after flashing (~30 seconds).

> **Mismatch protection:** the tool blocks generation mismatches to prevent a soft-brick — a Gen 1
> `ACE_V…` image on an AVATA printer, and, when the printer reports the model of the connected ACE
> (shown as `model=…` in the Test Connection log), any image that does not match that ACE.
> Use the `.bin` that matches your ACE.

## Recovery

### Flash Tool Recovery (recommended)

In most cases a bad flash can be recovered using the same flash tool. Flash the original Anycubic
firmware for your generation from the [`originalFirmware/`](../originalFirmware/) folder
(`ACE_V1.3.863…` for Gen 1, `ACE2_V1.1.31…` for Gen 2) — as long as the ACE bootloader is still
intact, this restores the unit to a working state.

> The bootloader is never overwritten by an OTA flash. If the application firmware fails to boot, the
> ACE falls back to bootloader/update mode and can be re-flashed over the network with the tool.

### SWD Recovery (last resort)

If the ACE no longer responds to the flash tool at all, it can be recovered via SWD using a GD-Link
adapter. See your printer's documentation for the SWD pinout on the ACE board. (Note: the ACE 2 Pro
SWD pads may not be easily accessible — try the Flash Tool recovery first.)
