# OTA Implementation Guide

This document describes the current OTA implementation and release mechanics in this repository.

## Current Versioning Model

- Firmware version source: `CMakeLists.txt` via `PROJECT_VER`
- Web app version source: `data/manifest.json`
- OTA entry point: `releases/latest/manifest.json`
- Current published version in this repository: `3.0.9`

Devices compare the version reported by `PROJECT_VER` against the version from `releases/latest/manifest.json`.

## Runtime Components

### OTA updater

Source files:

- `src/network/ota_updater.h`
- `src/network/ota_updater.cpp`

Responsibilities:

- Fetch `releases/latest/manifest.json`
- Compare manifest version to current firmware version
- Download LittleFS and firmware artifacts
- Validate sizes and SHA-256 hashes
- Stage and apply updates safely
- Report status and progress to the web UI

### Web interface

Relevant files:

- `data/configuration.html`
- `data/script.js`
- `src/control/display_state.h`
- `src/network/websocket_handler.cpp`

Responsibilities:

- Trigger OTA checks from the configuration page
- Surface OTA status, version, and progress
- Reflect updater state via WebSocket JSON

## Manifest Format

Current manifest schema:

```json
{
  "version": "X.Y.Z",
  "description": "Release X.Y.Z",
  "firmware": "firmware.bin",
  "littlefs": "littlefs.bin",
  "secure_version": 0,
  "artifacts": {
    "firmware": {
      "path": "firmware.bin",
      "size": 123456,
      "sha256": "<firmware-sha256>"
    },
    "littlefs": {
      "path": "littlefs.bin",
      "size": 456789,
      "sha256": "<littlefs-sha256>"
    }
  }
}
```

Required fields for current updater behavior:

- `version`
- `description`
- `firmware`
- `littlefs`
- `secure_version`
- `artifacts.firmware.path`
- `artifacts.firmware.size`
- `artifacts.firmware.sha256`
- `artifacts.littlefs.path`
- `artifacts.littlefs.size`
- `artifacts.littlefs.sha256`

## Release Tree

```text
releases/
├── latest/
│   ├── firmware.bin
│   ├── littlefs.bin
│   └── manifest.json
├── vX.Y.Z/
│   ├── firmware.bin
│   ├── littlefs.bin
│   └── manifest.json
└── older v*/ folders
```

`releases/latest/` is the active OTA pointer. Versioned folders are historical snapshots.

## Release Procedure

### Manual

1. Update `CMakeLists.txt` version.
2. Update `data/manifest.json` version.
3. Update service worker tokens in the HTML files under `data/`.
4. Update the release-facing docs before publishing artifacts.
5. Run `pio run`.
6. Run `pio run -t buildfs`.
7. Copy the resulting `firmware.bin` and `littlefs.bin` to:
   - `releases/latest/`
   - `releases/vX.Y.Z/`
8. Update both manifest files with exact sizes and SHA-256 values.
9. Push the repository to GitHub with the refreshed docs and OTA artifacts in the same repo state.
10. Verify OTA check behavior from a device.

### Scripted

```bash
scripts/ota_release.sh --version X.Y.Z
```

Use the script when publishing to the dedicated OTA repository path configured by `OTA_REPO_DIR`. The script should update current release docs before it copies OTA artifacts or pushes GitHub state.

## Verification

After a release update:

- `pio run` succeeds.
- `pio run -t buildfs` succeeds.
- `releases/latest/manifest.json` is valid JSON.
- Hashes in both manifests match the binaries in their folders.
- The configuration page still reports OTA state correctly.
- Devices can fetch the `latest` manifest and identify a newer version when appropriate.

## Notes on Historical Content

Older release folders may use legacy manifest schemas or list features that are no longer present in the current firmware. Keep them as historical archives unless you explicitly intend to rewrite old releases.

Last updated: 2026-03-14
