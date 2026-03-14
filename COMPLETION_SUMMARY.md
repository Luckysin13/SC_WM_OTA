# Current Repository Summary

## Status

The repository is aligned to the current `3.0.6` firmware and OTA release state.

## What Was Updated

- Firmware version aligned to `3.0.6`.
- Web app version aligned to `3.0.6`.
- Service worker cache-busting tokens updated across the HTML pages.
- `releases/latest/` refreshed with the current firmware and LittleFS artifacts.
- Current versioned OTA release snapshot: `releases/v3.0.6/`.
- `releases/latest/manifest.json` updated with current size and SHA-256 metadata.
- Root documentation rewritten to match the current repository layout and OTA workflow.

## Current Release Metadata

- Firmware size: `1215892`
- LittleFS size: `458752`
- Firmware SHA-256: `6861bfec947f4018cf784b55951c946ef26453f717d3ab1a5fd222760287e672`
- LittleFS SHA-256: `1ec799c6e579c4949c4bd048f77c07d09e13f45bdf40e201fa1e963214ab6db2`

## Current Release Paths

- `releases/latest/manifest.json`
- `releases/latest/firmware.bin`
- `releases/latest/littlefs.bin`
- `releases/v3.0.6/manifest.json`
- `releases/v3.0.6/firmware.bin`
- `releases/v3.0.6/littlefs.bin`

## Current Behavior Notes

- OTA updater remains active and points at `releases/latest/manifest.json`.
- Keep Warm, Done Alarm, PID autotuning, OTA update flow, and history features remain intact.
- `Pit Temp Low` and `Open Lid Detection` have been removed from current firmware and UI.

## Verification Performed

- `pio run` completed successfully on `3.0.6`.
- `pio run -t buildfs` completed successfully on `3.0.6`.
- OTA release manifests were updated to current artifact hashes.
- Root docs were rewritten to remove stale `v1.0.0` bootstrap instructions.

## Remaining Intentional Historical Content

Older `releases/v*/` folders are preserved as archives. They may still reference features or metadata formats that no longer apply to the current firmware.

Last updated: 2026-03-14
