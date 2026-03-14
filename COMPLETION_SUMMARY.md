# Current Repository Summary

## Status

The repository is aligned to the current `3.0.8` firmware and OTA release state.

## What Was Updated

- Firmware version aligned to `3.0.8`.
- Web app version aligned to `3.0.8`.
- Service worker cache-busting tokens updated across the HTML pages.
- `releases/latest/` refreshed with the current firmware and LittleFS artifacts.
- Current versioned OTA release snapshot: `releases/v3.0.8/`.
- `releases/latest/manifest.json` updated with current size and SHA-256 metadata.
- Root documentation rewritten to match the current repository layout and OTA workflow.

## Current Release Metadata

- Firmware size: `1215892`
- LittleFS size: `458752`
- Firmware SHA-256: `f1ad365f215c79c9a9ed0cb2c789b1a9d9000abc0f221bc2237a6236687ddaa7`
- LittleFS SHA-256: `9c6e24abf22029415c0710a12b19e75e91b0bedc9eeb814791beb3e32513f237`

## Current Release Paths

- `releases/latest/manifest.json`
- `releases/latest/firmware.bin`
- `releases/latest/littlefs.bin`
- `releases/v3.0.8/manifest.json`
- `releases/v3.0.8/firmware.bin`
- `releases/v3.0.8/littlefs.bin`

## Current Behavior Notes

- OTA updater remains active and points at `releases/latest/manifest.json`.
- Keep Warm, Done Alarm, PID autotuning, OTA update flow, and history features remain intact.
- `Pit Temp Low` and `Open Lid Detection` have been removed from current firmware and UI.

## Verification Performed

- `pio run` completed successfully on `3.0.8`.
- `pio run -t buildfs` completed successfully on `3.0.8`.
- OTA release manifests were updated to current artifact hashes.
- Root docs were rewritten to remove stale `v1.0.0` bootstrap instructions.

## Remaining Intentional Historical Content

Older `releases/v*/` folders are preserved as archives. They may still reference features or metadata formats that no longer apply to the current firmware.

Last updated: 2026-03-14
