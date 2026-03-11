
# Serial upload (PlatformIO)

```bash
# Prefer PlatformIO's own virtualenv install (VS Code extension uses this).
# On some distros `/usr/bin/pio` is an old PlatformIO Core (e.g. 4.x) that can crash.
# The fallback form below also works when `PIO` is not exported in your current shell.
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"

# upload firmware + LittleFS
"${PIO:-$HOME/.platformio/penv/bin/pio}" run -t upload --upload-port /dev/ttyUSB1 && "${PIO:-$HOME/.platformio/penv/bin/pio}" run -t uploadfs --upload-port /dev/ttyUSB1

# upload firmware only
"${PIO:-$HOME/.platformio/penv/bin/pio}" run -t upload --upload-port /dev/ttyUSB1

# upload LittleFS only
"${PIO:-$HOME/.platformio/penv/bin/pio}" run -t uploadfs --upload-port /dev/ttyUSB1

# build LittleFS image
"${PIO:-$HOME/.platformio/penv/bin/pio}" run -t buildfs

# (optional) gzip assets before building fs (if your UI expects *.gz)
find data -type f ! -name "*.gz" -exec gzip -9 -k -f {} \; && "${PIO:-$HOME/.platformio/penv/bin/pio}" run -t buildfs

# monitor serial output
"${PIO:-$HOME/.platformio/penv/bin/pio}" device monitor -p /dev/ttyUSB1 -b 115200

# clean build
"${PIO:-$HOME/.platformio/penv/bin/pio}" run -t clean

# buildfs + uploadfs + upload firmware
"${PIO:-$HOME/.platformio/penv/bin/pio}" run -t buildfs && "${PIO:-$HOME/.platformio/penv/bin/pio}" run -t uploadfs --upload-port /dev/ttyUSB1 && "${PIO:-$HOME/.platformio/penv/bin/pio}" run -t upload --upload-port /dev/ttyUSB1
```

---

# OTA publish to GitHub (SC_WM_OTA repo)

The device fetches these URLs (see `MANIFEST_URL` in `src/network/ota_updater.h`):
- `releases/latest/manifest.json`
- `releases/latest/firmware.bin`
- `releases/latest/littlefs.bin` (field name is `littlefs` for backward compatibility; the file is a LittleFS image)

Prereqs:
- You have a local clone of `Luckysin13/SC_WM_OTA` (the OTA hosting repo).
- Firmware is signed automatically during `pio run` via `scripts/sign_firmware.py`.
- Do NOT paste tokens into this file or your shell history.

## Step 1: Bump version in the firmware repo

```bash
# Run from the firmware repo root
cd SC_WiFi_toWM---Copy-SunnyDay_AntiG5th-main-OTA-fixes
VERSION="2.1.6"

sed -i -E 's/(CURRENT_VERSION[[:space:]]*=[[:space:]]*)"[^"]*"/\1"'"$VERSION"'"/' src/network/ota_updater.h
sed -i -E 's/(PROJECT_VER[[:space:]]*)"[^"]*"/\1"'"$VERSION"'"/' CMakeLists.txt
```

## Step 2: Build artifacts (firmware + LittleFS)

```bash
PIO="$HOME/.platformio/penv/bin/pio"

"$PIO" --version
"$PIO" run -e esp32dev
"$PIO" run -e esp32dev -t buildfs

# If `$PIO --version` fails, reinstall/upgrade PlatformIO (avoid the distro `/usr/bin/pio`).
```

## Step 3: (Optional) Verify signature

```bash
"$HOME/.platformio/penv/bin/python" "$HOME/.platformio/packages/tool-esptoolpy/espsecure.py" verify_signature \
    --version 1 \
    --keyfile signature_verification_key.bin \
    .pio/build/esp32dev/firmware.bin
```

## Step 4: Copy artifacts + update manifest in the OTA repo

```bash
# Set this to wherever you cloned the OTA repo
OTA_REPO_DIR="$HOME/Documents/SC_WM_OTA"

: "${VERSION:?Set VERSION in Step 1 first}"
CODE_VERSION=$(sed -n 's/.*CURRENT_VERSION[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' src/network/ota_updater.h | head -n1)
if [ "$VERSION" != "$CODE_VERSION" ]; then
    echo "VERSION mismatch: Step1 VERSION=$VERSION, code CURRENT_VERSION=$CODE_VERSION"
    exit 1
fi

mkdir -p "$OTA_REPO_DIR/releases/v$VERSION" "$OTA_REPO_DIR/releases/latest"

cp .pio/build/esp32dev/firmware.bin "$OTA_REPO_DIR/releases/v$VERSION/firmware.bin"
cp .pio/build/esp32dev/littlefs.bin "$OTA_REPO_DIR/releases/v$VERSION/littlefs.bin"

printf '{\n  "version": "%s",\n  "description": "Release %s",\n  "firmware": "firmware.bin",\n  "littlefs": "littlefs.bin"\n}\n' \
    "$VERSION" "$VERSION" > "$OTA_REPO_DIR/releases/v$VERSION/manifest.json"

cp "$OTA_REPO_DIR/releases/v$VERSION/firmware.bin" "$OTA_REPO_DIR/releases/latest/firmware.bin"
cp "$OTA_REPO_DIR/releases/v$VERSION/littlefs.bin" "$OTA_REPO_DIR/releases/latest/littlefs.bin"
cp "$OTA_REPO_DIR/releases/v$VERSION/manifest.json" "$OTA_REPO_DIR/releases/latest/manifest.json"
```

## Step 5: Commit + push to GitHub

Recommended (SSH remote):

```bash
: "${OTA_REPO_DIR:=$HOME/Documents/SC_WM_OTA}"
test -d "$OTA_REPO_DIR/.git" || { echo "Invalid OTA_REPO_DIR: $OTA_REPO_DIR"; exit 1; }
cd "$OTA_REPO_DIR"
git remote set-url origin git@github.com:Luckysin13/SC_WM_OTA.git
git add -A
git diff --cached --quiet || git commit -m "Release v$VERSION (signed firmware+LittleFS)"
git pull --rebase --autostash origin main
git push origin main
```

Alternative (token via env var):

```bash
: "${OTA_REPO_DIR:=$HOME/Documents/SC_WM_OTA}"
test -d "$OTA_REPO_DIR/.git" || { echo "Invalid OTA_REPO_DIR: $OTA_REPO_DIR"; exit 1; }
cd "$OTA_REPO_DIR"

# Export in your shell (don’t paste into scripts/docs):
# export GITHUB_TOKEN="..."

git remote set-url origin https://github.com/Luckysin13/SC_WM_OTA.git
git add -A
git diff --cached --quiet || git commit -m "Release v$VERSION (signed firmware+LittleFS)"
git pull --rebase --autostash origin main
git push "https://x-access-token:${GITHUB_TOKEN}@github.com/Luckysin13/SC_WM_OTA.git" main
```