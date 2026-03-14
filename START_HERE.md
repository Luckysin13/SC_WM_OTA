# Start Here

Use this file as the quick maintainer entry point for the current OTA repository.

## Current State

- Firmware source version: `3.0.9`
- Current OTA release: `releases/v3.0.9/`
- Latest OTA pointer: `releases/latest/`
- OTA updater is already implemented in firmware.
- Filesystem packaging uses LittleFS.

## Most Common Tasks

### Build firmware

```bash
pio run
```

### Build LittleFS image

```bash
pio run -t buildfs
```

### Upload current build to hardware

```bash
pio run -t upload --upload-port /dev/ttyUSB1
pio run -t uploadfs --upload-port /dev/ttyUSB1
```

### Publish a new OTA release

```bash
scripts/ota_release.sh --version X.Y.Z
```

If you are publishing manually, update the version in source, refresh the release-facing docs first, rebuild, refresh `releases/latest/`, create `releases/vX.Y.Z/`, and update the manifest hashes before pushing GitHub state.

## Read Next

- `README.md` for the current repository overview.
- `SETUP_INSTRUCTIONS.md` for GitHub publishing setup.
- `IMPLEMENTATION_GUIDE.md` for OTA architecture and release details.
- `MASTER_CHECKLIST.md` for the release process checklist.
- `FILE_MANIFEST.md` for the key file map.

## Current Release Artifacts

```text
releases/latest/
  firmware.bin
  littlefs.bin
  manifest.json

releases/vX.Y.Z/
  firmware.bin
  littlefs.bin
  manifest.json
```

## Important Notes

- `releases/latest/manifest.json` is what devices check first.
- Keep `CMakeLists.txt`, `data/manifest.json`, current release docs, and HTML service worker version tokens in sync.
- Historical `releases/v*/` folders are archives and should usually remain unchanged.

Last updated: 2026-03-14
