# SC_WM OTA Firmware Repository

This repository contains the current SC_WM firmware source, web assets, OTA updater implementation, and published OTA release artifacts for the ESP32-based smoker controller.

## Current Release

- Current source version: `3.0.7`
- Latest OTA manifest: `releases/latest/manifest.json`
- Current versioned OTA release: `releases/v3.0.7/`
- Filesystem format: LittleFS

### Release Artifacts

- `releases/latest/firmware.bin`
- `releases/latest/littlefs.bin`
- `releases/latest/manifest.json`
- `releases/v3.0.6/firmware.bin`
- `releases/v3.0.6/littlefs.bin`
- `releases/v3.0.6/manifest.json`

### 3.0.6 Notes

- Refreshed OTA release metadata and repository documentation.
- Aligned the release tree with the current firmware and LittleFS artifacts.
- OTA dry-run and OTA release status-bar buttons now target this repository directly.
- OTA release validation now blocks unresolved or diverged git states before publish.

## Repository Layout

```text
.
├── data/                    Web UI files packed into LittleFS
├── releases/
│   ├── latest/              OTA pointers used by devices in the field
│   ├── v1.0.0/ ...          Historical releases
│   └── v3.0.6/              Current versioned release
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

Update the release-facing docs first, then use the helper script to validate the docs, build the artifacts, and publish the full GitHub repo state:

```bash
scripts/ota_release.sh --version X.Y.Z
```

The helper updates current release docs, verifies they are aligned, and refuses to copy OTA artifacts into `releases/latest/` or push GitHub state until the docs are ready.
On a successful scripted release, it also stages and commits the release changes automatically with a generated commit message.

### Manual

1. Bump the firmware version in `CMakeLists.txt`.
2. Bump the PWA version in `data/manifest.json`.
3. Update the service worker version tokens in the HTML pages under `data/`.
4. Update the release-facing docs so the repo describes the new release before publishing it.
5. Build firmware with `pio run`.
6. Build LittleFS with `pio run -t buildfs`.
7. Copy the resulting `firmware.bin` and `littlefs.bin` into both:
   - `releases/latest/`
   - `releases/vX.Y.Z/`
8. Update both manifests with the correct version, sizes, and SHA-256 hashes.
9. Verify OTA update checks against `releases/latest/manifest.json`.
10. Commit and push the updated docs and release artifacts together.

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

When publishing a release, the GitHub repo should receive the refreshed top-level docs in the same push as the updated `releases/latest/` and `releases/vX.Y.Z/` artifacts.

## Verification Checklist

- `pio run` completes successfully.
- `pio run -t buildfs` completes successfully.
- `releases/latest/manifest.json` matches the current artifact hashes.
- `releases/vX.Y.Z/manifest.json` matches the current artifact hashes.
- `data/manifest.json` matches the current web version.
- The refreshed top-level release docs are included in the same commit as the OTA artifact update.
- Devices can still fetch and parse `releases/latest/manifest.json`.

## Historical Releases

Older `releases/v*/` folders are kept as historical artifacts. They may contain older metadata formats or features that no longer exist in the current firmware. Update historical release folders only if you intentionally want to rewrite history.

Last updated: 2026-03-14
