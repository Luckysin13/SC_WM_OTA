# Current Repository Summary

## Status

The repository is aligned to the current `3.0.4` firmware and OTA release state.

## What Was Updated

- Firmware version bumped from `3.0.3` to `3.0.4`.
- Web app version updated to `3.0.4`.
- Service worker cache-busting tokens updated across the HTML pages.
- `releases/latest/` refreshed with the current firmware and LittleFS artifacts.
- `releases/v3.0.4/` created as the current versioned OTA release.
- `releases/latest/manifest.json` updated with current size and SHA-256 metadata.
- Root documentation rewritten to match the current repository layout and OTA workflow.

## Current Release Metadata

- Firmware size: `1215892`
- LittleFS size: `458752`
- Firmware SHA-256: `4a4935985c10a9bcfd86a0aa6390c895438568b6d6ff15ae6915162858c30c7a`
- LittleFS SHA-256: `ee770ab18adbbc8b286268884dce8831c01a868b741a3941a929e7bbce82feb8`

## Current Release Paths

- `releases/latest/manifest.json`
- `releases/latest/firmware.bin`
- `releases/latest/littlefs.bin`
- `releases/v3.0.4/manifest.json`
- `releases/v3.0.4/firmware.bin`
- `releases/v3.0.4/littlefs.bin`

## Current Behavior Notes

- OTA updater remains active and points at `releases/latest/manifest.json`.
- Keep Warm, Done Alarm, PID autotuning, OTA update flow, and history features remain intact.
- `Pit Temp Low` and `Open Lid Detection` have been removed from current firmware and UI.

## Verification Performed

- `pio run` completed successfully on `3.0.4`.
- `pio run -t buildfs` completed successfully on `3.0.4`.
- OTA release manifests were updated to current artifact hashes.
- Root docs were rewritten to remove stale `v1.0.0` bootstrap instructions.

## Remaining Intentional Historical Content

Older `releases/v*/` folders are preserved as archives. They may still reference features or metadata formats that no longer apply to the current firmware.

Last updated: 2026-03-14
