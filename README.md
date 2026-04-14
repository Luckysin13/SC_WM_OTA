# OSSC  OTA Firmware Repository

This repository is published as a OTA branch for the OSSC smoker controller.

## Current Release

- Current source version: 5.0.6

## OTA Branch Layout

```text
├── LICENSE
├── README.md
├── releases/
│   ├── latest/              OTA used by devices in the field

```

## Current Features

- Web UI
- Real-time updates
- OTA firmware updates
- Persistent storage diagnostics UI
- Viewer with reset state, heap diagnostics, crash-pattern analysis, export, and clear actions
- PID temperature control
- Wi-Fi configuration and connectivity management
- OTA updates
- Local-network-only device interaction (AP/STA)
- Meat Done and Keep Warm Options

## Historical Releases

`releases/v*/` folders may contain older features that no longer exist in the current firmware.

Last updated: 04-08-26


# Open Source Smoker Controller

[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
![Build Status](https://img.shields.io/badge/build-TODO-lightgrey)
[![Latest Release](https://img.shields.io/github/v/release/Luckysin13/SD_SC_Main?display_name=tag)](https://github.com/Luckysin13/SD_SC_Main/releases)
![Hardware](https://img.shields.io/badge/hardware-ESP32%20%2B%20ADS1015-blue)

Browser-based ESP32 smoker controller firmware with live temperature monitoring, PID-driven fan control, onboard WiFi setup, historical charting, troubleshooting tools, and OTA update support.

> Replace the build badge with a GitHub Actions badge after a workflow is added under `.github/workflows/`.

## Features

- Real-time pit and meat temperature monitoring from the web dashboard.
- PID fan control with manual override support.
- End-of-cook and keep-warm options.
- WiFi onboarding page with scan, DHCP, and static IP support.
- Historical charting UI backed by on-device storage.
- OTA firmware update flow and manual OTA recovery tools.
- Diagnostic and crash-log page for reset tracking and health checks.
- LittleFS-hosted web UI flashed with the project image.

## Screenshots And Diagrams

![Dashboard screenshot placeholder](docs/images/dashboard-placeholder.png)

![Wiring diagram placeholder](docs/images/wiring-placeholder.png)

> Replace the placeholder image paths above with real screenshots or wiring images when they are available.

## Quick Start

### Prerequisites

- ESP-IDF 5.5.4 installed locally.
- An ESP32 target board.
- USB access to the device for initial flashing.
- A working ESP-IDF environment export script, typically `~/esp/esp-idf/export.sh`.

### Build And Flash

```bash
git clone https://github.com/Luckysin13/SD_SC_Main.git
cd SD_SC_Main
source ~/esp/esp-idf/export.sh
idf.py set-target esp32
idf.py build flash monitor
```

The web assets in `data/` are staged into a LittleFS image during the build and included in the normal project flash flow.

### Useful Commands

```bash
idf.py build
idf.py flash
idf.py monitor
idf.py fullclean
idf.py littlefs-flash
bash scripts/ota_release.sh --yes
```

## Hardware Wiring And BOM

Populate this table with the exact parts and wiring used by your build.

| Item          | Part / Model | Connection / Value | Notes |
| :------------ | :----------- | :----------------- | :---- |
| MCU Board     |              |                    |       |
| ADC           |              |                    |       |
| Pit Probe     |              |                    |       |
| Meat Probe    |              |                    |       |
| Fan / Blower  |              |                    |       |
| Power Supply  |              |                    |       |
| Misc Hardware |              |                    |       |

Current firmware defaults worth documenting against your hardware:

- Reset / SSID clear input: GPIO 14
- Fan PWM output: GPIO 2
- WiFi status LED: GPIO 18
- I2C SDA: GPIO 21
- I2C SCL: GPIO 22
- PWM frequency: 1000 Hz
- ADC type expected by the firmware: ADS1015-compatible

## First Boot And WiFi Onboarding

On first boot, or after WiFi credentials are erased, the controller can expose an access point for setup.

- Default AP SSID: `SMOKER CONTROLLER`
- Default AP password: `88888888`
- Fallback setup address: `http://192.168.4.1`
- Local hostname hint shown in the UI: `http://smoker.local`

Suggested onboarding flow:

1. Power the controller and join the setup access point.
2. Open the WiFi Setup page and scan for nearby networks or enter credentials manually.
3. Choose DHCP or enter a static IP.
4. Save the configuration and let the device reboot.
5. Reconnect through `smoker.local` or the assigned IP address.

WiFi LED behavior can help with setup:

- Blinking indicates AP mode or AP+STA without an active STA connection.
- Solid indicates a connected STA session.

## Web UI Overview

The LittleFS-hosted web UI is split into focused pages:

- `index.html`: dashboard with meat temp, pit temp, pit setpoint, and fan speed.
- `wifi.html`: WiFi scan, credential entry, DHCP/static IP, and credential erase flow.
- `options.html`: end-of-cook behavior, meat-done alarm, and keep-warm settings.
- `history.html`: Chart.js-based history view with zoom, pan, and clear actions.
- `configuration.html`: probe offsets, timezone selection, OTA check/update, PID and reset controls.
- `TroubleShooting.html`: reset history, health checks, debug payloads, and manual OTA recovery.

## OTA Updates

The project includes OTA support for both firmware and LittleFS assets, plus a release helper script at `scripts/ota_release.sh`.

- In-device OTA controls are exposed in the configuration and troubleshooting pages.
- Release artifacts are organized around a manifest-driven flow.
- The current OTA publishing flow expects a companion release repository.

> This README intentionally keeps OTA deployment details brief. Expand this section later if you want a maintainer-facing release guide.

## Architecture Overview

At a high level, the firmware is organized into these areas:

- Application startup initializes fault handling, hardware, storage, networking, sensors, PID state, and runtime tasks.
- Control logic reads pit and meat temperatures, runs PID control, and updates fan output.
- Networking manages WiFi modes, onboarding, and browser-driven control flows.
- Storage uses NVS and LittleFS for persistent settings, assets, and diagnostics.
- Web assets are staged from `data/` and packed into the `littlefs` partition during the build.
- OTA logic downloads manifest-indexed releases and applies updates on-device.

Flash layout summary:

- `nvs`: persistent settings, `0x4000`
- `otadata`: OTA state, `0x2000`
- `app0`: OTA slot A, `0x180000`
- `app1`: OTA slot B, `0x180000`
- `littlefs`: web assets and diagnostics, `0x0F0000`

## Troubleshooting And Crash Logs

If the controller is not behaving as expected:

1. Open the troubleshooting page and refresh the diagnostics summary.
2. Review reset entries and health checks.
3. Copy or download the debug payload before making changes.
4. If networking is unstable, erase stored credentials and repeat WiFi onboarding.
5. If the UI does not match the firmware, rebuild and reflash to ensure the latest LittleFS image is installed.

Common places to look while developing:

- Serial monitor output via `idf.py monitor`
- The troubleshooting web page for reset history and manual OTA tools
- `scripts/decode_backtrace.sh` for post-crash analysis

## Development Setup

For local firmware development:

```bash
source ~/esp/esp-idf/export.sh
idf.py build
idf.py flash
idf.py monitor
```

Notes for contributors:

- This repo currently has no GitHub Actions workflow configured, so the build badge is a placeholder.
- The project version is defined in `CMakeLists.txt`.
- The build stages and compresses web assets before creating the LittleFS image.
- `idf_ext.py` prints app flash-usage information after builds and flashes.

## Roadmap And Known Limitations

- Publish a complete hardware BOM and wiring guide.
- Replace placeholder screenshots and diagrams.
- Add CI so the build badge points to a real workflow.
- Expand release documentation for maintainers if OTA handoff needs to be reproducible by others.
- Document hardware compatibility more precisely across board and probe variants.

## Hardware Compatibility Note

This firmware is currently documented around an ESP32-based controller using an ADS1015-style ADC, two temperature probes, PWM fan control, and an onboard web UI. If you are targeting different boards, probe types, or fan hardware, validate the pin map and thermistor assumptions before flashing.

## License

This project is licensed under the MIT License. See `LICENSE` for details.
