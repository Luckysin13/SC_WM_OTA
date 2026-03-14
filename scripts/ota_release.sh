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
RESTORE_RELEASE_DOCS=1
declare -a PWA_BACKUPS=()
declare -a RELEASE_DOC_BACKUPS=()
REPO_ROOT="$(pwd -P)"
TODAY="$(date +%Y-%m-%d)"
declare -a REQUIRED_RELEASE_DOCS=(
  README.md
)
declare -a SOURCE_RELEASE_STAGE_PATHS=(
  CMakeLists.txt
  README.md
  data/manifest.json
  data/index.html
  data/alarms.html
  data/graph.html
  data/configuration.html
  data/wifi.html
)

usage() {
  echo "Usage: $0 [--check] [--yes] [--version X.Y.Z]"
}

canonical_dir() {
  (
    cd "$1"
    pwd -P
  )
}

git_path_exists() {
  local repo_dir="$1"
  local git_path="$2"
  [[ -e "$(git -C "$repo_dir" rev-parse --git-path "$git_path")" ]]
}

require_repo_ready_for_release() {
  local repo_dir="$1"
  local repo_label="$2"

  if ! git -C "$repo_dir" rev-parse --git-dir >/dev/null 2>&1; then
    echo "Invalid $repo_label git repository: $repo_dir"
    exit 1
  fi

  if git_path_exists "$repo_dir" rebase-apply || \
     git_path_exists "$repo_dir" rebase-merge || \
     git_path_exists "$repo_dir" MERGE_HEAD || \
     git_path_exists "$repo_dir" CHERRY_PICK_HEAD || \
     git_path_exists "$repo_dir" REVERT_HEAD || \
     [[ -n "$(git -C "$repo_dir" diff --name-only --diff-filter=U)" ]]; then
    echo "Release blocked: $repo_label has unresolved merge/rebase state. Resolve it before running OTA Release."
    exit 1
  fi
}

require_repo_syncable_with_origin() {
  local repo_dir="$1"
  local repo_label="$2"
  local local_head
  local remote_head
  local merge_base

  if ! git -C "$repo_dir" remote get-url origin >/dev/null 2>&1; then
    return 0
  fi

  if ! git -C "$repo_dir" fetch origin main --quiet; then
    echo "Release blocked: failed to fetch origin/main for $repo_label."
    exit 1
  fi

  if ! git -C "$repo_dir" rev-parse --verify origin/main >/dev/null 2>&1; then
    return 0
  fi

  local_head="$(git -C "$repo_dir" rev-parse HEAD)"
  remote_head="$(git -C "$repo_dir" rev-parse origin/main)"
  merge_base="$(git -C "$repo_dir" merge-base HEAD origin/main)"

  if [[ "$local_head" == "$remote_head" || "$merge_base" == "$remote_head" ]]; then
    return 0
  fi

  if [[ "$merge_base" == "$local_head" ]]; then
    echo "Release blocked: $repo_label is behind origin/main. Sync the repo before running OTA Release."
    exit 1
  fi

  echo "Release blocked: $repo_label has diverged from origin/main. Reconcile the branch before running OTA Release."
  exit 1
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

backup_release_docs() {
  local file
  local backup
  for file in "${REQUIRED_RELEASE_DOCS[@]}"; do
    backup="$(mktemp)"
    cp "$file" "$backup"
    RELEASE_DOC_BACKUPS+=("$file:$backup")
  done
}

restore_release_docs() {
  local entry
  local file
  local backup
  for entry in "${RELEASE_DOC_BACKUPS[@]}"; do
    file="${entry%%:*}"
    backup="${entry#*:}"
    cp "$backup" "$file"
    rm -f "$backup"
  done
  RELEASE_DOC_BACKUPS=()
}

require_release_docs_present() {
  local file
  for file in "${REQUIRED_RELEASE_DOCS[@]}"; do
    if [[ ! -f "$file" ]]; then
      echo "Missing required release document: $file"
      exit 1
    fi
  done
}

sync_release_docs() {
  RELEASE_VERSION="$1" \
  PREVIOUS_VERSION="$2" \
  RELEASE_DATE="$3" \
  FIRMWARE_SIZE="$4" \
  LITTLEFS_SIZE="$5" \
  FIRMWARE_SHA256="$6" \
  LITTLEFS_SHA256="$7" \
  "$PYTHON_BIN" <<'PY'
import os
import pathlib
import re

root = pathlib.Path(".")
version = os.environ["RELEASE_VERSION"]
previous_version = os.environ["PREVIOUS_VERSION"]
release_date = os.environ["RELEASE_DATE"]
firmware_size = os.environ["FIRMWARE_SIZE"]
littlefs_size = os.environ["LITTLEFS_SIZE"]
firmware_sha = os.environ["FIRMWARE_SHA256"]
littlefs_sha = os.environ["LITTLEFS_SHA256"]

def replace_or_fail(path: pathlib.Path, pattern: str, replacement: str) -> None:
    content = path.read_text(encoding="utf-8")
    updated, count = re.subn(pattern, replacement, content, count=1, flags=re.MULTILINE)
    if count != 1:
        raise SystemExit(f"Failed to update {path} with pattern: {pattern}")
    path.write_text(updated, encoding="utf-8")

replace_or_fail(
    root / "README.md",
    r"^- Current source version: `[^`]+`$",
    f"- Current source version: `{version}`",
)
replace_or_fail(
    root / "README.md",
    r"^- Current versioned OTA release: `releases/v[^`]+/`$",
    f"- Current versioned OTA release: `releases/v{version}/`",
)
replace_or_fail(
    root / "README.md",
    r"^Last updated: .*?$",
    f"Last updated: {release_date}",
)
PY
}

verify_release_docs() {
  EXPECTED_VERSION="$1" \
  EXPECTED_FIRMWARE_SIZE="$2" \
  EXPECTED_LITTLEFS_SIZE="$3" \
  EXPECTED_FIRMWARE_SHA256="$4" \
  EXPECTED_LITTLEFS_SHA256="$5" \
  "$PYTHON_BIN" <<'PY'
import os
import pathlib
import re
import sys

root = pathlib.Path(".")
version = os.environ["EXPECTED_VERSION"]
firmware_size = os.environ["EXPECTED_FIRMWARE_SIZE"]
littlefs_size = os.environ["EXPECTED_LITTLEFS_SIZE"]
firmware_sha = os.environ["EXPECTED_FIRMWARE_SHA256"]
littlefs_sha = os.environ["EXPECTED_LITTLEFS_SHA256"]

required_files = [
    "README.md",
]
for relative in required_files:
    if not (root / relative).is_file():
        print(f"Missing required release document: {relative}", file=sys.stderr)
        raise SystemExit(1)

checks = {
    "README.md": [
        rf"^- Current source version: `{re.escape(version)}`$",
        rf"^- Current versioned OTA release: `releases/v{re.escape(version)}/`$",
    ],
}

for relative, patterns in checks.items():
    content = (root / relative).read_text(encoding="utf-8")
    for pattern in patterns:
        if not re.search(pattern, content, flags=re.MULTILINE):
            print(f"Release document validation failed for {relative}: missing pattern {pattern}", file=sys.stderr)
            raise SystemExit(1)
PY
}

sync_publish_repo_docs() {
  local target_dir="$1"
  local target_root
  local file
  target_root="$(canonical_dir "$target_dir")"
  if [[ "$target_root" == "$REPO_ROOT" ]]; then
    return 0
  fi

  for file in "${REQUIRED_RELEASE_DOCS[@]}"; do
    cp "$file" "$target_root/$file"
  done
}

verify_publish_repo_docs() {
  local target_dir="$1"
  local target_root
  local file
  target_root="$(canonical_dir "$target_dir")"
  if [[ "$target_root" == "$REPO_ROOT" ]]; then
    return 0
  fi

  for file in "${REQUIRED_RELEASE_DOCS[@]}"; do
    if ! cmp -s "$file" "$target_root/$file"; then
      echo "Publish repo documentation mismatch: $file"
      exit 1
    fi
  done
}

commit_repo_changes() {
  local repo_dir="$1"
  local commit_message="$2"
  shift 2
  local -a stage_paths=("$@")

  if [[ ! -d "$repo_dir/.git" ]]; then
    echo "Cannot commit changes: $repo_dir is not a git repository"
    exit 1
  fi

  (
    cd "$repo_dir"
    if [[ "${#stage_paths[@]}" -eq 0 ]]; then
      echo "Cannot commit changes: no stage paths specified for $repo_dir"
      exit 1
    fi
    git add -- "${stage_paths[@]}"
    git diff --cached --quiet || git commit -m "$commit_message"
  )
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

sw_pattern = re.compile(r"/sw\.js\?v=[^'\"]+")
scriptwifi_pattern = re.compile(r"scriptwifi\.js\?v=[^'\"]+")
for relative_path in [
    "data/index.html",
    "data/alarms.html",
    "data/graph.html",
    "data/configuration.html",
]:
    path = root / relative_path
    content = path.read_text(encoding="utf-8")
    if not sw_pattern.search(content):
        raise SystemExit(f"Failed to update service worker version token in {relative_path}")
    updated = sw_pattern.sub(f"/sw.js?v={version}", content)
    path.write_text(updated, encoding="utf-8")

wifi_path = root / "data" / "wifi.html"
wifi_content = wifi_path.read_text(encoding="utf-8")
if not sw_pattern.search(wifi_content):
    raise SystemExit("Failed to update service worker version token in data/wifi.html")
if not scriptwifi_pattern.search(wifi_content):
    raise SystemExit("Failed to update scriptwifi version token in data/wifi.html")
wifi_updated = sw_pattern.sub(f"/sw.js?v={version}", wifi_content)
wifi_updated = scriptwifi_pattern.sub(f"scriptwifi.js?v={version}", wifi_updated)
wifi_path.write_text(wifi_updated, encoding="utf-8")
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

sw_pattern = re.compile(r"/sw\.js\?v=([^'\"]+)")
for relative_path in [
    "data/index.html",
    "data/alarms.html",
    "data/graph.html",
    "data/configuration.html",
]:
    content = (root / relative_path).read_text(encoding="utf-8")
    match = sw_pattern.search(content)
    if not match:
        print(f"Missing service worker version token in {relative_path}", file=sys.stderr)
        raise SystemExit(1)
    if match.group(1) != expected:
        print(
            f"Service worker version mismatch in {relative_path}: expected {expected}, found {match.group(1)}",
            file=sys.stderr,
        )
        raise SystemExit(1)

wifi_content = (root / "data" / "wifi.html").read_text(encoding="utf-8")
wifi_sw_match = sw_pattern.search(wifi_content)
if not wifi_sw_match:
        print("Missing service worker version token in data/wifi.html", file=sys.stderr)
        raise SystemExit(1)
if wifi_sw_match.group(1) != expected:
        print(
            f"Service worker version mismatch in data/wifi.html: expected {expected}, found {wifi_sw_match.group(1)}",
            file=sys.stderr,
        )
        raise SystemExit(1)

scriptwifi_pattern = re.compile(r"scriptwifi\.js\?v=([^'\"]+)")
scriptwifi_match = scriptwifi_pattern.search(wifi_content)
if not scriptwifi_match:
        print("Missing scriptwifi version token in data/wifi.html", file=sys.stderr)
        raise SystemExit(1)
if scriptwifi_match.group(1) != expected:
        print(
            f"scriptwifi version mismatch in data/wifi.html: expected {expected}, found {scriptwifi_match.group(1)}",
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

require_repo_ready_for_release "$REPO_ROOT" "source repo"
require_repo_ready_for_release "$OTA_REPO_DIR" "OTA repo"
require_repo_syncable_with_origin "$OTA_REPO_DIR" "OTA repo"

ORIGINAL_PROJECT_VERSION="$(extract_project_version)"
RESTORE_PROJECT_VERSION=1
tmp_manifest=""

backup_pwa_files
backup_release_docs

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
  if [[ "$RESTORE_RELEASE_DOCS" -eq 1 && "${#RELEASE_DOC_BACKUPS[@]}" -gt 0 ]]; then
    restore_release_docs
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
    if [[ "$CHECK_ONLY" -ne 1 ]]; then
      echo "VERSION must be newer than latest ($LATEST_VERSION)."
      exit 1
    fi
  fi
  HIGHEST="$(printf "%s\n%s\n" "$LATEST_VERSION" "$VERSION" | sort -V | tail -n1)"
  if [[ "$VERSION" != "$LATEST_VERSION" && "$HIGHEST" != "$VERSION" ]]; then
    echo "VERSION '$VERSION' is lower than latest '$LATEST_VERSION'."
    exit 1
  fi
fi

require_release_docs_present

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

sync_release_docs "$VERSION" "$ORIGINAL_PROJECT_VERSION" "$TODAY" \
  "$FIRMWARE_SIZE" "$LITTLEFS_SIZE" "$FIRMWARE_SHA256" "$LITTLEFS_SHA256"
verify_release_docs "$VERSION" "$FIRMWARE_SIZE" "$LITTLEFS_SIZE" \
  "$FIRMWARE_SHA256" "$LITTLEFS_SHA256"

tmp_manifest="$(mktemp)"
build_manifest "$tmp_manifest"
"$PYTHON_BIN" -m json.tool "$tmp_manifest" >/dev/null

if [[ "$CHECK_ONLY" -eq 1 ]]; then
  echo "Release check passed for v$VERSION"
  echo "Firmware size: $FIRMWARE_SIZE bytes"
  echo "LittleFS size: $LITTLEFS_SIZE bytes"
  exit 0
fi

sync_publish_repo_docs "$OTA_REPO_DIR"
verify_publish_repo_docs "$OTA_REPO_DIR"

mkdir -p "$OTA_REPO_DIR/releases/v$VERSION" "$OTA_REPO_DIR/releases/latest"
rm -f "$OTA_REPO_DIR/releases/v$VERSION/firmware.bin" \
  "$OTA_REPO_DIR/releases/v$VERSION/littlefs.bin" \
  "$OTA_REPO_DIR/releases/v$VERSION/manifest.json"
cp .pio/build/esp32dev/firmware.bin "$OTA_REPO_DIR/releases/v$VERSION/firmware.bin"
cp .pio/build/esp32dev/littlefs.bin "$OTA_REPO_DIR/releases/v$VERSION/littlefs.bin"
cp "$tmp_manifest" "$OTA_REPO_DIR/releases/v$VERSION/manifest.json"

cp "$OTA_REPO_DIR/releases/v$VERSION/firmware.bin" "$OTA_REPO_DIR/releases/latest/firmware.bin"
cp "$OTA_REPO_DIR/releases/v$VERSION/littlefs.bin" "$OTA_REPO_DIR/releases/latest/littlefs.bin"
cp "$OTA_REPO_DIR/releases/v$VERSION/manifest.json" "$OTA_REPO_DIR/releases/latest/manifest.json"

RESTORE_PROJECT_VERSION=0
RESTORE_PWA_FILES=0
RESTORE_RELEASE_DOCS=0

cd "$OTA_REPO_DIR"
declare -a OTA_REPO_STAGE_PATHS=(
  README.md
  "releases/latest/firmware.bin"
  "releases/latest/littlefs.bin"
  "releases/latest/manifest.json"
  "releases/v$VERSION/firmware.bin"
  "releases/v$VERSION/littlefs.bin"
  "releases/v$VERSION/manifest.json"
)
if [[ "$(canonical_dir "$PWD")" == "$REPO_ROOT" ]]; then
  commit_repo_changes "$PWD" "Release v$VERSION (OTA artifacts + README)" \
    "${SOURCE_RELEASE_STAGE_PATHS[@]}" "${OTA_REPO_STAGE_PATHS[@]}"
else
  commit_repo_changes "$PWD" "Release v$VERSION (OTA artifacts + README)" "${OTA_REPO_STAGE_PATHS[@]}"
fi
git push origin HEAD:main

if [[ "$(canonical_dir "$PWD")" != "$REPO_ROOT" ]]; then
  commit_repo_changes "$REPO_ROOT" "Prepare release v$VERSION source update" "${SOURCE_RELEASE_STAGE_PATHS[@]}"
fi

echo "Release v$VERSION completed."
