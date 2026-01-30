# OTA Repository Setup Instructions

## Overview

The `SC_WM_OTA` repository has been created locally with compiled firmware binaries ready for deployment. This guide walks you through pushing it to GitHub and preparing for implementation of OTA updates in #6.

## Repository Structure

```
SC_WM_OTA/
├── README.md                      (Comprehensive documentation)
├── .gitignore                      (Git ignore rules)
├── setup_github.sh                 (GitHub push automation script)
└── releases/
    └── v1.0.0/
        ├── firmware.bin            (Main firmware - 972 KB)
        ├── spiffs.bin              (Web interface/data - 1.4 MB)
        └── manifest.json           (Version metadata)
```

## Contents

### firmware.bin
- Main SC_WM application firmware
- Size: 972 KB
- Contains all control logic, WebSocket handler, PID controller, etc.
- Target address: 0x10000 in ESP32 flash

### spiffs.bin
- SPIFFS filesystem with web interface
- Size: 1.4 MB
- Contains: index.html, configuration.html, script.js, styles, etc.
- Target address: 0x290000 in ESP32 flash

### manifest.json
- Version metadata for OTA system
- Contains: version number, timestamps, file sizes, feature list, changelog
- Used by firmware update checker to identify new versions

## Step 1: Create GitHub Personal Access Token

GitHub no longer supports password authentication for git operations. You need a Personal Access Token:

1. Go to: https://github.com/settings/tokens
2. Click "Generate new token (classic)"
3. Name it: `SC_WM_OTA_Deploy`
4. Select scopes:
   - ✅ `repo` (Full control of private repositories)
5. Click "Generate token"
6. **Copy the token immediately** (you won't see it again!)

Example token format: `ghp_XXXX_EXAMPLE_TOKEN_XXXX`

## Step 2: Push Repository to GitHub

Run the setup script with your token:

```bash
cd /home/jonathan/Downloads/SC_WM_OTA
./setup_github.sh <YOUR_GITHUB_TOKEN>
```

Example:
```bash
./setup_github.sh ghp_1234567890abcdefghijklmnopqrstuv
```

This script will:
1. Configure git remote with your token
2. Push to GitHub
3. Verify the push was successful
4. Clean up credentials from local config

Expected output:
```
✅ Successfully pushed to GitHub!

Repository is now available at:
  https://github.com/Luckysin13/SC_WM_OTA

Next: Set repository to PUBLIC in GitHub Settings
```

## Step 3: Make Repository Public

1. Go to: https://github.com/Luckysin13/SC_WM_OTA/settings
2. Scroll down to "Danger Zone"
3. Click "Change repository visibility"
4. Select "Public"
5. Confirm

This allows OTA clients to fetch firmware updates without authentication.

## Step 4: Verify Repository

After pushing, verify the repository is set up correctly:

```bash
# Check remote
cd /home/jonathan/Downloads/SC_WM_OTA
git remote -v
# Should show: https://github.com/Luckysin13/SC_WM_OTA.git

# Check latest commit
git log --oneline
# Should show: Initial OTA release v1.0.0 with firmware binaries

# Check files
ls -lh releases/v1.0.0/
# Should show firmware.bin, spiffs.bin, manifest.json
```

## Repository URLs

After setup, you'll have:

- **Repository**: https://github.com/Luckysin13/SC_WM_OTA
- **Raw Firmware**: https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/v1.0.0/firmware.bin
- **Raw SPIFFS**: https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/v1.0.0/spiffs.bin
- **Manifest**: https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/v1.0.0/manifest.json

These URLs will be used in **Task #6** for implementing OTA updates.

## For Task #6 (OTA Implementation)

When implementing OTA updates in the firmware (#6), you'll need to:

1. **Create an OTA update handler** that:
   - Periodically checks `manifest.json` for new versions
   - Compares against current firmware version
   - Downloads updates from GitHub raw content URLs
   - Validates checksums (optional but recommended)
   - Performs the update via ESP32 OTA APIs

2. **Add a web interface** in `configuration.html` to:
   - Check for updates manually
   - Show current version vs available version
   - Initiate OTA update
   - Show progress during update
   - Handle rollback on failure

3. **Key firmware URLs** to use:
   ```
   https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/v1.0.0/manifest.json
   https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/v1.0.0/firmware.bin
   https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/releases/v1.0.0/spiffs.bin
   ```

## Troubleshooting

### "Authentication failed" error
- Verify your PAT is correct
- Check it hasn't expired at https://github.com/settings/tokens
- Ensure you selected the `repo` scope

### "Repository not found" error
- Repository must be created on GitHub first
- Visit https://github.com/Luckysin13/SC_WM_OTA to verify it exists
- Check repository name spelling

### Can't download from raw URLs
- Verify repository is set to **PUBLIC**
- Check URLs in a browser - they should download the files
- Wait a few minutes for GitHub to cache the new files

## Next Steps

1. ✅ OTA repository created locally with binaries
2. ✅ Documentation complete
3. 📋 **NEXT**: Run `./setup_github.sh` with your PAT to push to GitHub
4. 📋 **THEN**: Make repository public
5. 📋 **Task #6**: Implement OTA update handler in firmware

## Reference Files

- **Local Repository**: `/home/jonathan/Downloads/SC_WM_OTA/`
- **Setup Script**: `/home/jonathan/Downloads/SC_WM_OTA/setup_github.sh`
- **README**: `/home/jonathan/Downloads/SC_WM_OTA/README.md`
- **Current Firmware**: `SC_WiFi_toWM---Copy-SunnyDay_AntiG4th-main/`

---

**Created**: 2026-01-16  
**Status**: Ready for GitHub push  
**Next Task**: Task #6 - Implement OTA Update Feature
