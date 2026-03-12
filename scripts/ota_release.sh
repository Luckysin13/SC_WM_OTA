#!/usr/bin/env bash
set -euo pipefail

PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
OTA_REPO_DIR="${OTA_REPO_DIR:-$HOME/Documents/SC_WM_OTA}"
ESPSECURE_PY="${ESPSECURE_PY:-$HOME/.platformio/packages/tool-esptoolpy/espsecure.py}"
PYTHON_BIN="${PYTHON_BIN:-$HOME/.platformio/penv/bin/python}"
REQUIRE_SIGNING="${REQUIRE_SIGNING:-1}"
CHECK_ONLY=0
AUTO_CONFIRM=0
VERSION="${VERSION:-}"
SECURE_VERSION="${SECURE_VERSION:-0}"
RESTORE_PWA_FILES=1
declare -a PWA_BACKUPS=()

usage() {
  echo "Usage: $0 [--check] [--yes] [--version X.Y.Z]"
}

extract_project_version() {
  sed -n 's/.*PROJECT_VER[[:space:]]*"\([^"]*\)".*/\1/p' CMakeLists.txt | head -n1
}

restore_project_version() {
  sed -i -E 's/(PROJECT_VER[[:space:]]*)"[^"]*"/\1"'"$1"'"/' CMakeLists.txt
}

backup_pwa_files() {
  local file
  local backup
  for file in data/manifest.json data/index.html data/wifi.html data/alarms.html data/graph.html data/configuration.html; do
    backup="$(mktemp)"
    cp "$file" "$backup"
    PWA_BACKUPS+=("$file:$backup")
  done
}

restore_pwa_files() {
  local entry
  local file
  local backup
  for entry in "${PWA_BACKUPS[@]}"; do
    file="${entry%%:*}"
    backup="${entry#*:}"
    cp "$backup" "$file"
    rm -f "$backup"
  done
  PWA_BACKUPS=()
}

sync_pwa_version_files() {
  RELEASE_VERSION="$1" "$PYTHON_BIN" <<'PY'
import json
import os
import pathlib
import re

version = os.environ["RELEASE_VERSION"]
root = pathlib.Path(".")

manifest_path = root / "data" / "manifest.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
manifest["version"] = version
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")

pattern = re.compile(r"/sw\.js\?v=[^'\"]+")
for relative_path in [
    "data/index.html",
    "data/wifi.html",
    "data/alarms.html",
    "data/graph.html",
    "data/configuration.html",
]:
    path = root / relative_path
    content = path.read_text(encoding="utf-8")
    updated = pattern.sub(f"/sw.js?v={version}", content)
    if updated == content:
        raise SystemExit(f"Failed to update service worker version token in {relative_path}")
    path.write_text(updated, encoding="utf-8")
PY
}

verify_pwa_version_files() {
  EXPECTED_VERSION="$1" "$PYTHON_BIN" <<'PY'
import json
import os
import pathlib
import re
import sys

expected = os.environ["EXPECTED_VERSION"]
root = pathlib.Path(".")

manifest_path = root / "data" / "manifest.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
if manifest.get("version") != expected:
    print(
        f"PWA manifest version mismatch: expected {expected}, found {manifest.get('version', '')}",
        file=sys.stderr,
    )
    raise SystemExit(1)

pattern = re.compile(r"/sw\.js\?v=([^'\"]+)")
for relative_path in [
    "data/index.html",
    "data/wifi.html",
    "data/alarms.html",
    "data/graph.html",
    "data/configuration.html",
]:
    content = (root / relative_path).read_text(encoding="utf-8")
    match = pattern.search(content)
    if not match:
        print(f"Missing service worker version token in {relative_path}", file=sys.stderr)
        raise SystemExit(1)
    if match.group(1) != expected:
        print(
            f"Service worker version mismatch in {relative_path}: expected {expected}, found {match.group(1)}",
            file=sys.stderr,
        )
        raise SystemExit(1)
PY
}

build_manifest() {
  local manifest_path="$1"
  MANIFEST_PATH="$manifest_path" VERSION="$VERSION" SECURE_VERSION="$SECURE_VERSION" \
  FIRMWARE_SIZE="$FIRMWARE_SIZE" FIRMWARE_SHA256="$FIRMWARE_SHA256" \
  LITTLEFS_SIZE="$LITTLEFS_SIZE" LITTLEFS_SHA256="$LITTLEFS_SHA256" \
  "$PYTHON_BIN" <<'PY'
import json
import os

manifest = {
    "version": os.environ["VERSION"],
    "description": f"Release {os.environ['VERSION']}",
    "firmware": "firmware.bin",
    "littlefs": "littlefs.bin",
    "secure_version": int(os.environ["SECURE_VERSION"]),
    "artifacts": {
        "firmware": {
            "path": "firmware.bin",
            "size": int(os.environ["FIRMWARE_SIZE"]),
            "sha256": os.environ["FIRMWARE_SHA256"],
        },
        "littlefs": {
            "path": "littlefs.bin",
            "size": int(os.environ["LITTLEFS_SIZE"]),
            "sha256": os.environ["LITTLEFS_SHA256"],
        },
    },
}

with open(os.environ["MANIFEST_PATH"], "w", encoding="ascii") as handle:
    json.dump(manifest, handle, indent=2)
    handle.write("\n")
PY
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --check)
      CHECK_ONLY=1
      ;;
    --yes)
      AUTO_CONFIRM=1
      ;;
    --version)
      shift
      VERSION="${1:-}"
      ;;
    --version=*)
      VERSION="${1#*=}"
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      usage
      exit 1
      ;;
  esac
  shift
done

if [[ ! -x "$PIO" ]]; then
  echo "PlatformIO not found at $PIO"
  exit 1
fi
if [[ ! -x "$PYTHON_BIN" ]]; then
  echo "Python not found at $PYTHON_BIN"
  exit 1
fi
if [[ ! -d "$OTA_REPO_DIR/.git" ]]; then
  echo "Invalid OTA repo: $OTA_REPO_DIR"
  exit 1
fi

ORIGINAL_PROJECT_VERSION="$(extract_project_version)"
RESTORE_PROJECT_VERSION=1
tmp_manifest=""

backup_pwa_files

cleanup() {
  if [[ -n "$tmp_manifest" && -f "$tmp_manifest" ]]; then
    rm -f "$tmp_manifest"
  fi
  if [[ "$RESTORE_PROJECT_VERSION" -eq 1 ]]; then
    restore_project_version "$ORIGINAL_PROJECT_VERSION"
  fi
  if [[ "$RESTORE_PWA_FILES" -eq 1 && "${#PWA_BACKUPS[@]}" -gt 0 ]]; then
    restore_pwa_files
  fi
}

trap cleanup EXIT

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

if [[ -z "$VERSION" && -n "$LATEST_VERSION" ]]; then
  if [[ -n "$NEXT_VERSION" ]]; then
    echo "Version preview: latest=$LATEST_VERSION, suggested next patch=$NEXT_VERSION"
    read -r -p "Enter new Version= [default $NEXT_VERSION]: " VERSION
  else
    echo "Version preview: latest=$LATEST_VERSION"
    read -r -p "Enter new Version= (latest in repo: $LATEST_VERSION): " VERSION
  fi
else
  if [[ -z "$VERSION" ]]; then
    read -r -p "Enter new Version=: " VERSION
  fi
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

if [[ "$AUTO_CONFIRM" -ne 1 ]]; then
  read -r -p "Proceed with release v$VERSION? [y/N]: " CONFIRM
  if [[ ! "$CONFIRM" =~ ^[Yy]$ ]]; then
    echo "Release cancelled by user."
    exit 1
  fi
fi

sed -i -E 's/(PROJECT_VER[[:space:]]*)"[^"]*"/\1"'"$VERSION"'"/' CMakeLists.txt
sync_pwa_version_files "$VERSION"

CODE_VERSION="$(extract_project_version)"
if [[ "$VERSION" != "$CODE_VERSION" ]]; then
  echo "VERSION mismatch after Step 1: VERSION=$VERSION, PROJECT_VER=$CODE_VERSION"
  exit 1
fi

verify_pwa_version_files "$VERSION"

"$PIO" --version
"$PIO" run -e esp32dev
"$PIO" run -e esp32dev -t buildfs

if [[ -f signature_verification_key.bin ]]; then
  "$PYTHON_BIN" "$ESPSECURE_PY" verify_signature \
    --version 1 \
    --keyfile signature_verification_key.bin \
    .pio/build/esp32dev/firmware.bin
else
  if [[ "$REQUIRE_SIGNING" == "1" ]]; then
    echo "Signing verification key missing: signature_verification_key.bin"
    exit 1
  fi
  echo "Skipping signature verification: signature_verification_key.bin not found"
fi

[[ -f .pio/build/esp32dev/firmware.bin ]] || { echo "Missing firmware artifact"; exit 1; }
[[ -f .pio/build/esp32dev/littlefs.bin ]] || { echo "Missing LittleFS artifact"; exit 1; }

FIRMWARE_SIZE="$(stat -c %s .pio/build/esp32dev/firmware.bin)"
LITTLEFS_SIZE="$(stat -c %s .pio/build/esp32dev/littlefs.bin)"
FIRMWARE_SHA256="$(sha256sum .pio/build/esp32dev/firmware.bin | awk '{print $1}')"
LITTLEFS_SHA256="$(sha256sum .pio/build/esp32dev/littlefs.bin | awk '{print $1}')"

tmp_manifest="$(mktemp)"
build_manifest "$tmp_manifest"
"$PYTHON_BIN" -m json.tool "$tmp_manifest" >/dev/null

if [[ "$CHECK_ONLY" -eq 1 ]]; then
  echo "Release check passed for v$VERSION"
  echo "Firmware size: $FIRMWARE_SIZE bytes"
  echo "LittleFS size: $LITTLEFS_SIZE bytes"
  exit 0
fi

mkdir -p "$OTA_REPO_DIR/releases/v$VERSION" "$OTA_REPO_DIR/releases/latest"
rm -f "$OTA_REPO_DIR/releases/v$VERSION/firmware.bin" \
  "$OTA_REPO_DIR/releases/v$VERSION/littlefs.bin" \
  "$OTA_REPO_DIR/releases/v$VERSION/spiffs.bin" \
  "$OTA_REPO_DIR/releases/v$VERSION/manifest.json"
cp .pio/build/esp32dev/firmware.bin "$OTA_REPO_DIR/releases/v$VERSION/firmware.bin"
cp .pio/build/esp32dev/littlefs.bin "$OTA_REPO_DIR/releases/v$VERSION/littlefs.bin"
cp "$tmp_manifest" "$OTA_REPO_DIR/releases/v$VERSION/manifest.json"

cp "$OTA_REPO_DIR/releases/v$VERSION/firmware.bin" "$OTA_REPO_DIR/releases/latest/firmware.bin"
cp "$OTA_REPO_DIR/releases/v$VERSION/littlefs.bin" "$OTA_REPO_DIR/releases/latest/littlefs.bin"
cp "$OTA_REPO_DIR/releases/v$VERSION/manifest.json" "$OTA_REPO_DIR/releases/latest/manifest.json"
rm -f "$OTA_REPO_DIR/releases/latest/spiffs.bin"

RESTORE_PROJECT_VERSION=0
RESTORE_PWA_FILES=0

cd "$OTA_REPO_DIR"
git add -A
git diff --cached --quiet || git commit -m "Release v$VERSION (signed firmware+LittleFS)"
git pull --rebase --autostash origin main
git push origin main

echo "Release v$VERSION completed."
