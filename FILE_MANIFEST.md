# File Manifest

This file maps the key files and directories that matter for building, packaging, publishing, and maintaining the current OTA firmware repository.

## Root Files

- `CMakeLists.txt`
  Firmware version source of truth via `PROJECT_VER`.

- `platformio.ini`
  PlatformIO environment, ESP-IDF configuration, and LittleFS selection.

- `README.md`
  Current repository overview and release workflow.

- `START_HERE.md`
  Quick maintainer orientation.

- `SETUP_INSTRUCTIONS.md`
  GitHub publishing setup and release validation.

- `IMPLEMENTATION_GUIDE.md`
  OTA architecture and manifest details.

- `COMPLETION_SUMMARY.md`
  Current repository status summary.

- `MASTER_CHECKLIST.md`
  Current release and publishing checklist.

- `setup_github.sh`
  Helper script for pushing this repository to GitHub.

## Source Tree

### `src/network/`

- `ota_updater.h`
  OTA updater interface.

- `ota_updater.cpp`
  OTA updater implementation, manifest parsing, and artifact verification.

- `websocket_handler.cpp`
  WebSocket command and state handling, including OTA-related interactions.

### `src/control/`

- `display_state.h`
  JSON payload state sent to the UI, including OTA-related display fields.

- `controller_state.h`
  Runtime control state used by PID and other control features.

### `src/main.cpp`

Main runtime loop and subsystem wiring.

## Web Assets

### `data/`

- `manifest.json`
  PWA metadata and web app version.

- `index.html`
  Main dashboard.

- `configuration.html`
  Configuration and OTA interaction page.

- `alarms.html`
  Alarm configuration page.

- `graph.html`
  Temperature history graph.

- `wifi.html`
  WiFi configuration page.

- `script.js`
  Primary client-side logic and WebSocket handling.

- `scriptwifi.js`
  WiFi page behavior.

- `style.css`
  Shared styling.

- `sw.js`
  Service worker for the PWA.

## Release Artifacts

### `releases/latest/`

Current OTA pointer used by deployed devices:

- `firmware.bin`
- `littlefs.bin`
- `manifest.json`

### `releases/vX.Y.Z/`

Current versioned release snapshot:

- `firmware.bin`
- `littlefs.bin`
- `manifest.json`

### `releases/v*/`

Historical releases retained for archive and rollback reference.

## Scripts

### `scripts/ota_release.sh`

Automates version synchronization, current release document updates, artifact generation, manifest creation, and publish-repo staging for a release.

### `scripts/sign_firmware.py`

Signs the firmware artifact after build.

### `scripts/ensure_signature_s.py`

Ensures expected signature assets are prepared during the build.

## Build Output

### `.pio/build/esp32dev/`

Generated PlatformIO build artifacts. These are inputs to the current OTA release publishing process but are not the canonical published OTA release location.

Last updated: 2026-03-14
