#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include "compat/compat.h"

// =============================================================================
// OTA UPDATER CLASS
// =============================================================================
// Handles Over-The-Air firmware updates from GitHub repository
// Checks for updates, downloads, and applies them safely
// =============================================================================

class OTAUpdater {
public:
  // Update status enum
  enum UpdateStatus {
    IDLE = 0,
    CHECKING = 1,
    UPDATE_AVAILABLE = 2,
    DOWNLOADING = 3,
    INSTALLING = 4,
    SUCCESS = 5,
    FAILED = 6,
    NO_UPDATE = 7
  };

  enum UpdatePhase {
    PHASE_NONE = 0,
    PHASE_SPIFFS = 1,
    PHASE_FIRMWARE = 2
  };

  // Current firmware version (matches releases/latest/manifest.json)
  static constexpr const char *CURRENT_VERSION = "2.1.7";
  
    // GitHub OTA repository URLs (latest pointers)
    static constexpr const char *MANIFEST_URL =
      "https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/"
      "releases/latest/manifest.json";

    static constexpr const char *FIRMWARE_URL =
      "https://raw.githubusercontent.com/Luckysin13/SC_WM_OTA/main/"
      "releases/latest/firmware.bin";

private:
  UpdateStatus status = IDLE;
  UpdatePhase phase = PHASE_NONE;
  String availableVersion = "";
  String firmwareUrl = "";
  String spiffsUrl = "";
  uint32_t availableSecureVersion = 0;
  String updateError = "";
  String currentVersionOverride = "";
  int updateProgress = 0; // 0-100
  int spiffsProgress = 0;  // 0-100
  int firmwareProgress = 0; // 0-100

public:
  // Initialize OTA updater
  void begin();

  // Override current version for comparison/reporting
  void setCurrentVersion(const String &version) { currentVersionOverride = version; }

  // Effective current version (override if set)
  String getCurrentVersion() const {
    return currentVersionOverride.length() > 0 ? currentVersionOverride
                                               : String(CURRENT_VERSION);
  }

  // Check for available updates (non-blocking)
  void checkForUpdates();

  // Get current update status
  UpdateStatus getStatus() const { return status; }

  // Get version to report to UI (switch to flashed version after success)
  String getReportedVersion() const {
    if (status == SUCCESS && availableVersion.length() > 0) {
      return availableVersion;
    }
    return getCurrentVersion();
  }

  // Get available version if update exists
  String getAvailableVersion() const { return availableVersion; }

  // Get available secure version if provided by manifest
  uint32_t getAvailableSecureVersion() const { return availableSecureVersion; }

  // Get URLs for latest artifacts
  String getFirmwareUrl() const { return firmwareUrl; }
  String getSpiffsUrl() const { return spiffsUrl; }

  // Get error message if update failed
  String getErrorMessage() const { return updateError; }

  // Get update progress (0-100)
  int getProgress() const { return updateProgress; }

  // Get update phase and per-artifact progress
  UpdatePhase getPhase() const { return phase; }
  int getSpiffsProgress() const { return spiffsProgress; }
  int getFirmwareProgress() const { return firmwareProgress; }

  // Initiate firmware download and installation
  // Returns true if update started, false if already in progress
  bool startUpdate();

  // Cancel ongoing update
  void cancelUpdate();

  // Cleanup after update (call in loop for state management)
  void update();

  // Check if device should reboot after successful update
  bool shouldReboot() const { return status == SUCCESS; }

private:
  // Fetch and parse manifest.json from GitHub
  bool fetchManifest(String &outVersion, String &outDescription,
                     String &outFirmware, String &outSpiffs,
                     uint32_t &outSecureVersion);

  // Download firmware binary and apply update
  bool downloadAndUpdate(const String &firmwareUrl);

  // Download LittleFS binary and apply update
  bool downloadAndUpdateSpiffs(const String &spiffsUrl);
};

#endif // OTA_UPDATER_H
