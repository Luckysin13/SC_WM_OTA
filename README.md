<<<<<<< HEAD
# SC_WM_OTA
Over The Air Firmware Updates
=======
# SC_WM OTA (Over-The-Air) Firmware Repository

Official firmware release repository for the Smoker Controller - WiFi Module (SC_WM).

## Overview

This repository contains compiled firmware binaries for remote update of SC_WM smoker controllers via Over-The-Air (OTA) updates through the web interface.

## Directory Structure

```
releases/
├── v1.0.0/
│   ├── firmware.bin      (Main application firmware)
│   ├── spiffs.bin        (Web interface and data files)
│   └── manifest.json     (Version metadata and checksums)
└── [future versions...]
```

## Latest Version

**Current Release**: v1.0.0

### Features
- ✅ Lid Open Detection
- ✅ PID Autotuning (Ziegler-Nichols)
- ✅ Ramp-to-Done (Keep Warm mode)
- ✅ Predictive Time-to-Done Estimation
- ✅ WebSocket Real-time Updates
- ✅ Temperature Calibration Offsets
- ✅ Network Configuration (STA/AP modes)

## Installation / Updating

### Via Web Interface (OTA)
1. Navigate to the SC_WM web interface at `http://<device-ip>/configuration.html`
2. Look for "Check for Updates" or "Update Firmware" section
3. The device will query this repository for the latest version
4. If updates available, click "Update" to initiate OTA update
5. Device will download and apply the update (takes ~2 minutes)
6. Device automatically reboots after successful update

### Manual Update (USB)
If OTA is not available, use PlatformIO:
```bash
pio run -t upload --upload-port /dev/ttyUSB0
pio run -t uploadfs --upload-port /dev/ttyUSB0
```

## File Descriptions

### firmware.bin
- Main application binary
- Size: ~1 MB
- Contains: PID controller, WebSocket handler, temperature management
- Flashed to address 0x10000 in ESP32 partition table

### spiffs.bin
- SPIFFS filesystem image
- Size: ~1.4 MB
- Contains: HTML pages, CSS, JavaScript, configuration files
- Flashed to address 0x290000 in ESP32 partition table

### manifest.json
- Version metadata and checksums
- Used by OTA update system to verify firmware integrity
- Contains version, timestamp, feature list, changelog

## Version History

### v1.0.0 (2026-01-16)
- Initial OTA release
- Core features: Temperature control, PID tuning, Time-to-Done prediction
- Network modes: STA (WiFi) and AP (Configuration portal)
- WebSocket live updates to web dashboard

## Hardware Requirements

- **Device**: ESP32-WROOM-32D (or compatible)
- **Memory**: 4 MB Flash minimum
- **Network**: WiFi 802.11 b/g/n
- **Sensors**: 2× DS18B20 Temperature Probes
- **Actuator**: PWM-controlled Fan (3-pin)

## Checksum Verification

To verify firmware integrity after download:

```bash
# Calculate SHA256
sha256sum releases/v1.0.0/firmware.bin
sha256sum releases/v1.0.0/spiffs.bin
```

Compare with checksums in `manifest.json`.

## Development

To contribute or compile custom firmware:

```bash
git clone https://github.com/Luckysin13/SC_WM.git
cd SC_WM
pio run         # Build
pio run -t upload --upload-port /dev/ttyUSB0  # Deploy
```

## Troubleshooting

### OTA Update Fails
1. Check WiFi connection stability
2. Ensure device has 50% free SPIFFS space
3. Check browser console for error messages
4. Try manual update via USB if OTA repeatedly fails

### Device Doesn't Boot After Update
1. Connect via USB and check serial output (115200 baud)
2. If corrupted, perform factory reset (see documentation)
3. Roll back to previous version via manual USB update

### Version Mismatch
Device keeps reverting to old version:
1. Clear browser cache
2. Hard refresh configuration page (Ctrl+Shift+R)
3. Check manifest.json for version conflicts

## Security Notes

- ⚠️ OTA updates should only be obtained from trusted sources
- 🔒 Consider HTTPS for production deployments
- 🔐 Implement firmware signature verification (future enhancement)
- 🛡️ Regularly update to latest version for security patches

## Support & Issues

- 📧 Report issues on GitHub Issues page
- 🐛 Include version number and error logs in bug reports
- 💡 Feature requests welcome via GitHub Discussions

## License

Firmware and OTA repository structure are part of the SC_WM project.

---

**Last Updated**: 2026-01-16
**Repository**: https://github.com/Luckysin13/SC_WM_OTA
>>>>>>> 1e26b57 (Initial OTA release v1.0.0 with firmware binaries)
