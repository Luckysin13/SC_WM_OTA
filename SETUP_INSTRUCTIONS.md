# GitHub Setup Instructions

This repository is intended to be published to GitHub as the OTA source for SC_WM devices.

## Expected Repository

- Repository: `https://github.com/Luckysin13/SC_WM_OTA`
- Branch: `main`
- Latest OTA manifest: `releases/latest/manifest.json`

## One-Time Setup

### 1. Create a GitHub Personal Access Token

Create a classic PAT at:

- `https://github.com/settings/tokens`

Recommended scope:

- `repo`

### 2. Push This Repository

From the repository root:

```bash
./setup_github.sh <YOUR_GITHUB_PAT>
```

The script updates the git remote, pushes `main`, and then removes the tokenized remote URL.

### 3. Verify Raw URLs

After push, verify these URLs:

- `https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/latest/manifest.json`
- `https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/latest/firmware.bin`
- `https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/latest/littlefs.bin`

## Publishing a New Release

### Scripted path

```bash
scripts/ota_release.sh --version 3.0.4
```

### Manual path

1. Update `CMakeLists.txt` project version.
2. Update `data/manifest.json` version.
3. Update the service worker cache-busting tokens in:
   - `data/index.html`
   - `data/configuration.html`
   - `data/graph.html`
   - `data/alarms.html`
   - `data/wifi.html`
4. Run `pio run`.
5. Run `pio run -t buildfs`.
6. Copy the artifacts into `releases/latest/` and `releases/vX.Y.Z/`.
7. Update both manifest files with exact sizes and SHA-256 hashes.
8. Commit and push.

## Validation

Before pushing, confirm:

- `pio run` passes.
- `pio run -t buildfs` passes.
- `releases/latest/manifest.json` points to the current version.
- `releases/vX.Y.Z/manifest.json` points to the same artifacts as the versioned folder.
- The repository contains the current firmware and LittleFS binaries.

## Notes

- The OTA updater uses `releases/latest/manifest.json` as the primary entry point.
- Historical release folders remain in place for rollback and traceability.
- The current repository uses LittleFS, not SPIFFS.

Last updated: 2026-03-14
