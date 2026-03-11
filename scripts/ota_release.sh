#!/usr/bin/env bash
set -euo pipefail

PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
OTA_REPO_DIR="${OTA_REPO_DIR:-$HOME/Documents/SC_WM_OTA}"

if [[ ! -x "$PIO" ]]; then
  echo "PlatformIO not found at $PIO"
  exit 1
fi
if [[ ! -d "$OTA_REPO_DIR/.git" ]]; then
  echo "Invalid OTA repo: $OTA_REPO_DIR"
  exit 1
fi

LATEST_VERSION="$(find "$OTA_REPO_DIR/releases" -maxdepth 1 -mindepth 1 -type d -name "v*" -printf "%f\n" 2>/dev/null | sed "s/^v//" | sort -V | tail -n1 || true)"
if [[ -z "$LATEST_VERSION" && -f "$OTA_REPO_DIR/releases/latest/manifest.json" ]]; then
  LATEST_VERSION="$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$OTA_REPO_DIR/releases/latest/manifest.json" | head -n1)"
fi
if [[ -z "$LATEST_VERSION" && -f "releases/latest/manifest.json" ]]; then
  LATEST_VERSION="$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' releases/latest/manifest.json | head -n1)"
fi

NEXT_VERSION=""
if [[ "$LATEST_VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  NEXT_VERSION="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$((BASH_REMATCH[3] + 1))"
fi

if [[ -n "$LATEST_VERSION" ]]; then
  if [[ -n "$NEXT_VERSION" ]]; then
    echo "Version preview: latest=$LATEST_VERSION, suggested next patch=$NEXT_VERSION"
    read -r -p "Enter new Version= [default $NEXT_VERSION]: " VERSION
  else
    echo "Version preview: latest=$LATEST_VERSION"
    read -r -p "Enter new Version= (latest in repo: $LATEST_VERSION): " VERSION
  fi
else
  read -r -p "Enter new Version=: " VERSION
fi

VERSION="$(printf "%s" "$VERSION" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
if [[ -z "$VERSION" && -n "$NEXT_VERSION" ]]; then
  VERSION="$NEXT_VERSION"
fi
if [[ -z "$VERSION" ]]; then
  echo "VERSION is required"
  exit 1
fi
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9]+)*$ ]]; then
  echo "Invalid VERSION '$VERSION'. Expected something like 2.1.7 or 2.1.7-rc1"
  exit 1
fi
if [[ -n "$LATEST_VERSION" ]]; then
  if [[ "$VERSION" == "$LATEST_VERSION" ]]; then
    echo "VERSION must be newer than latest ($LATEST_VERSION)."
    exit 1
  fi
  HIGHEST="$(printf "%s\n%s\n" "$LATEST_VERSION" "$VERSION" | sort -V | tail -n1)"
  if [[ "$HIGHEST" != "$VERSION" ]]; then
    echo "VERSION '$VERSION' is lower than latest '$LATEST_VERSION'."
    exit 1
  fi
fi

read -r -p "Proceed with release v$VERSION? [y/N]: " CONFIRM
if [[ ! "$CONFIRM" =~ ^[Yy]$ ]]; then
  echo "Release cancelled by user."
  exit 1
fi

sed -i -E 's/(CURRENT_VERSION[[:space:]]*=[[:space:]]*)"[^"]*"/\1"'"$VERSION"'"/' src/network/ota_updater.h
sed -i -E 's/(PROJECT_VER[[:space:]]*)"[^"]*"/\1"'"$VERSION"'"/' CMakeLists.txt

CODE_VERSION="$(sed -n 's/.*CURRENT_VERSION[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' src/network/ota_updater.h | head -n1)"
if [[ "$VERSION" != "$CODE_VERSION" ]]; then
  echo "VERSION mismatch after Step 1: VERSION=$VERSION, CURRENT_VERSION=$CODE_VERSION"
  exit 1
fi

"$PIO" --version
"$PIO" run -e esp32dev
"$PIO" run -e esp32dev -t buildfs

if [[ -f signature_verification_key.bin ]]; then
  "$HOME/.platformio/penv/bin/python" "$HOME/.platformio/packages/tool-esptoolpy/espsecure.py" verify_signature \
    --version 1 \
    --keyfile signature_verification_key.bin \
    .pio/build/esp32dev/firmware.bin
else
  echo "Skipping signature verification: signature_verification_key.bin not found"
fi

[[ -f .pio/build/esp32dev/firmware.bin ]] || { echo "Missing firmware artifact"; exit 1; }
[[ -f .pio/build/esp32dev/littlefs.bin ]] || { echo "Missing LittleFS artifact"; exit 1; }

mkdir -p "$OTA_REPO_DIR/releases/v$VERSION" "$OTA_REPO_DIR/releases/latest"
cp .pio/build/esp32dev/firmware.bin "$OTA_REPO_DIR/releases/v$VERSION/firmware.bin"
cp .pio/build/esp32dev/littlefs.bin "$OTA_REPO_DIR/releases/v$VERSION/littlefs.bin"

printf '{\n  "version": "%s",\n  "description": "Release %s",\n  "firmware": "firmware.bin",\n  "littlefs": "littlefs.bin"\n}\n' \
  "$VERSION" "$VERSION" > "$OTA_REPO_DIR/releases/v$VERSION/manifest.json"

cp "$OTA_REPO_DIR/releases/v$VERSION/firmware.bin" "$OTA_REPO_DIR/releases/latest/firmware.bin"
cp "$OTA_REPO_DIR/releases/v$VERSION/littlefs.bin" "$OTA_REPO_DIR/releases/latest/littlefs.bin"
cp "$OTA_REPO_DIR/releases/v$VERSION/manifest.json" "$OTA_REPO_DIR/releases/latest/manifest.json"

cd "$OTA_REPO_DIR"
git add -A
git diff --cached --quiet || git commit -m "Release v$VERSION (signed firmware+LittleFS)"
git pull --rebase --autostash origin main
git push origin main

echo "Release v$VERSION completed."
