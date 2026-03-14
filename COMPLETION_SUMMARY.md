# Current Repository Summary

## Status

The repository is aligned to the current `3.0.9` firmware and OTA release state.

## What Was Updated

- Firmware version aligned to `3.0.9`.
- Web app version aligned to `3.0.9`.
- Service worker cache-busting tokens updated across the HTML pages.
- `releases/latest/` refreshed with the current firmware and LittleFS artifacts.
- Current versioned OTA release snapshot: `releases/v3.0.9/`.
- `releases/latest/manifest.json` updated with current size and SHA-256 metadata.
- Root documentation rewritten to match the current repository layout and OTA workflow.

## Current Release Metadata

- Firmware size: `1215860`
- LittleFS size: `458752`
- Firmware SHA-256: `a99eb7163de82ceebd0cf0c7337fdb6e23bd8814e277dae17d89a5a1e6232faf`
- LittleFS SHA-256: `2df44912e2e5c5c91d4eb3158dac892c25e25b1629e4189c7de5bc77f4e89e91`

## Current Release Paths

- `releases/latest/manifest.json`
- `releases/latest/firmware.bin`
- `releases/latest/littlefs.bin`
- `releases/v3.0.9/manifest.json`
- `releases/v3.0.9/firmware.bin`
- `releases/v3.0.9/littlefs.bin`

## Current Behavior Notes

- OTA updater remains active and points at `releases/latest/manifest.json`.
- Keep Warm, Done Alarm, PID autotuning, OTA update flow, and history features remain intact.
- `Pit Temp Low` and `Open Lid Detection` have been removed from current firmware and UI.

## Verification Performed

- `pio run` completed successfully on `3.0.9`.
- `pio run -t buildfs` completed successfully on `3.0.9`.
- OTA release manifests were updated to current artifact hashes.
- Root docs were rewritten to remove stale `v1.0.0` bootstrap instructions.

## Remaining Intentional Historical Content

Older `releases/v*/` folders are preserved as archives. They may still reference features or metadata formats that no longer apply to the current firmware.

Last updated: 2026-03-14
