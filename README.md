# SC_WM OTA Firmware Repository

This repository is published as a minimal OTA branch for the ESP32-based smoker controller. A real OTA release keeps only the `releases/` tree plus `.gitignore`, `CMakeLists.txt`, `LICENSE`, and `README.md` at the repo root.

## Current Release

- Current source version: `3.0.10`
- Latest OTA manifest: `releases/latest/manifest.json`
- Current versioned OTA release: `releases/v3.0.10/`
- Filesystem format: LittleFS

### Release Artifacts

- `releases/latest/firmware.bin`
- `releases/latest/littlefs.bin`
- `releases/latest/manifest.json`
- `releases/v3.0.7/firmware.bin`
- `releases/v3.0.7/littlefs.bin`
- `releases/v3.0.7/manifest.json`

### 3.0.7 Notes

- Refreshed OTA release metadata and repository documentation.
- Aligned the release tree with the current firmware and LittleFS artifacts.
- OTA dry-run and OTA release status-bar buttons build from this source checkout and publish against `origin/main`.
- When the source checkout and OTA publish repo are the same path, the release helper now uses a temporary `origin/main` worktree so local branch divergence does not block publishing.

## OTA Branch Layout

```text
.
├── .gitignore
├── CMakeLists.txt           Project version copied into the OTA publish branch
├── LICENSE
├── README.md
├── releases/
│   ├── latest/              OTA pointers used by devices in the field
│   ├── v1.0.0/ ...          Historical releases
│   └── v3.0.7/              Current versioned release
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

Use the helper script to validate the release files, build the artifacts, and publish the minimum OTA runtime set that the device actually needs:

```bash
scripts/ota_release.sh --version X.Y.Z
```

The helper temporarily updates the firmware and web-app version metadata required to build the release, verifies the generated artifacts, restores the source-tree metadata locally, prunes the OTA publish branch to the allowed root entries, syncs `.gitignore`, `CMakeLists.txt`, `LICENSE`, and `README.md`, and then publishes these OTA runtime files to `releases/latest/`:

- `firmware.bin`
- `littlefs.bin`
- `manifest.json`

On a successful scripted release, it stages the resulting OTA branch snapshot and commits the whitelist-enforced layout with a generated commit message. If you run it from the source checkout itself, the script publishes through a temporary checkout of `origin/main` instead of requiring your local source branch to match the OTA branch layout.

### Manual

1. Temporarily bump the firmware version in `CMakeLists.txt`.
2. Temporarily bump the PWA version in `data/manifest.json` and the web asset version tokens under `data/`.
3. Build firmware with `pio run`.
4. Build LittleFS with `pio run -t buildfs`.
5. Copy the resulting `firmware.bin` and `littlefs.bin` into `releases/latest/`.
6. Generate `releases/latest/manifest.json` with the correct version, sizes, and SHA-256 hashes.
7. Verify OTA update checks against `releases/latest/manifest.json`.
8. Commit and push the updated `releases/latest/` files.

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
