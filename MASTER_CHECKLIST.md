# Release Checklist

Use this checklist when preparing and publishing a new OTA release from this repository.

## Version Sync

- [ ] Update `CMakeLists.txt` `PROJECT_VER`.
- [ ] Update `data/manifest.json` version.
- [ ] Update service worker version tokens in:
- [ ] `data/index.html`
- [ ] `data/configuration.html`
- [ ] `data/graph.html`
- [ ] `data/alarms.html`
- [ ] `data/wifi.html`
- [ ] Update any other release-facing docs that reference the current version.

## Build

- [ ] Run `pio run`.
- [ ] Run `pio run -t buildfs`.
- [ ] Confirm signed firmware was produced.
- [ ] Confirm current `littlefs.bin` was produced.

## Publish Artifacts

- [ ] Create `releases/vX.Y.Z/`.
- [ ] Copy `firmware.bin` into `releases/latest/` and `releases/vX.Y.Z/`.
- [ ] Copy `littlefs.bin` into `releases/latest/` and `releases/vX.Y.Z/`.
- [ ] Update `releases/latest/manifest.json`.
- [ ] Create or update `releases/vX.Y.Z/manifest.json`.
- [ ] Verify file sizes and SHA-256 hashes match the copied binaries.

## Validate OTA Behavior

- [ ] Check that the configuration page still reports OTA status correctly.
- [ ] Verify devices can fetch `releases/latest/manifest.json`.
- [ ] Verify version comparison behaves as expected.
- [ ] If hardware is available, perform a real OTA update test.

## Documentation

- [ ] Update `README.md` current release section.
- [ ] Update `START_HERE.md` if maintainer workflow changed.
- [ ] Update `SETUP_INSTRUCTIONS.md` if GitHub publishing changed.
- [ ] Update `IMPLEMENTATION_GUIDE.md` if manifest schema or OTA flow changed.
- [ ] Update `COMPLETION_SUMMARY.md` with current release facts.
- [ ] Update `FILE_MANIFEST.md` if file layout changed.

## GitHub

- [ ] Commit the source, docs, and release artifacts.
- [ ] Push `main` to GitHub.
- [ ] Verify raw GitHub URLs for the latest manifest and binaries.
- [ ] Leave historical release folders unchanged unless intentionally rewriting history.

Last updated: 2026-03-14
