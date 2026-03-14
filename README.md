# SC_WM OTA Firmware Repository

This repository contains the current SC_WM firmware source, web assets, OTA updater implementation, and published OTA release artifacts for the ESP32-based smoker controller.

## Current Release

- Current source version: `3.0.4`
- Latest OTA manifest: `releases/latest/manifest.json`
- Current versioned OTA release: `releases/v3.0.4/`
- Filesystem format: LittleFS

### Release Artifacts

- `releases/latest/firmware.bin`
- `releases/latest/littlefs.bin`
- `releases/latest/manifest.json`
- `releases/v3.0.4/firmware.bin`
- `releases/v3.0.4/littlefs.bin`
- `releases/v3.0.4/manifest.json`

### 3.0.4 Notes

- Refreshed OTA release metadata and repository documentation.
- Aligned the release tree with the current firmware and LittleFS artifacts.
- Removed the dormant `Pit Temp Low` alarm stub.
- Removed `Open Lid Detection` from firmware, protocol, and UI.

## Repository Layout

```text
.
├── data/                    Web UI files packed into LittleFS
├── releases/
│   ├── latest/              OTA pointers used by devices in the field
│   ├── v1.0.0/ ...          Historical releases
│   └── v3.0.4/              Current versioned release
├── scripts/
│   ├── ota_release.sh       Release helper for publishing OTA artifacts
│   ├── sign_firmware.py     Post-build signing helper
│   └── ensure_signature_s.py
├── src/
│   ├── control/             PID, state, history, temperature logic
│   ├── network/             WiFi, WebSocket, OTA updater
│   └── ...
├── CMakeLists.txt           Project version source of truth for firmware
├── platformio.ini           PlatformIO environment and filesystem config
└── setup_github.sh          Helper for pushing this repo to GitHub
```

## Current Features

- OTA update checks and downloads via GitHub raw content.
- Signed firmware build pipeline.
- LittleFS web interface packaging.
- PID autotuning.
- Keep Warm / Done Alarm workflow.
- Temperature history retention and graphing.
- WiFi STA/AP operation with WebSocket UI updates.
- Temperature calibration offsets.

## Build Commands

### Firmware

```bash
pio run
```

### LittleFS Image

```bash
pio run -t buildfs
```

### Upload to Device

```bash
pio run -t upload --upload-port /dev/ttyUSB1
pio run -t uploadfs --upload-port /dev/ttyUSB1
```

Adjust the upload port if your device is not on `/dev/ttyUSB1`.

## OTA Operation

The device checks:

- `https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/latest/manifest.json`

The manifest points to:

- `firmware.bin`
- `littlefs.bin`

The OTA updater verifies file size and SHA-256 values from the manifest before marking an update as successful.

## Release Workflow

### Recommended

Use the helper script when publishing a new version to the OTA repository path configured in `scripts/ota_release.sh`:

```bash
scripts/ota_release.sh --version 3.0.4
```

### Manual

1. Bump the firmware version in `CMakeLists.txt`.
2. Bump the PWA version in `data/manifest.json`.
3. Update the service worker version tokens in the HTML pages under `data/`.
4. Build firmware with `pio run`.
5. Build LittleFS with `pio run -t buildfs`.
6. Copy the resulting `firmware.bin` and `littlefs.bin` into both:
   - `releases/latest/`
   - `releases/vX.Y.Z/`
7. Update both manifests with the correct version, sizes, and SHA-256 hashes.
8. Verify OTA update checks against `releases/latest/manifest.json`.
9. Commit and push the updated release artifacts and docs.

## GitHub Publishing

If this repository is not already connected to GitHub, use:

```bash
./setup_github.sh <YOUR_GITHUB_PAT>
```

Expected repository:

- `https://github.com/Luckysin13/SC_WM_OTA`

Raw OTA URLs:

- `https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/latest/manifest.json`
- `https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/latest/firmware.bin`
- `https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/latest/littlefs.bin`

## Verification Checklist

- `pio run` completes successfully.
- `pio run -t buildfs` completes successfully.
- `releases/latest/manifest.json` matches the current artifact hashes.
- `releases/v3.0.4/manifest.json` matches the current artifact hashes.
- `data/manifest.json` matches the current web version.
- Devices can still fetch and parse `releases/latest/manifest.json`.

## Historical Releases

Older `releases/v*/` folders are kept as historical artifacts. They may contain older metadata formats or features that no longer exist in the current firmware. Update historical release folders only if you intentionally want to rewrite history.

Last updated: 2026-03-14
