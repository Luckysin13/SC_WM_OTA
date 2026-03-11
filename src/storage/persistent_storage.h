#ifndef PERSISTENT_STORAGE_H
#define PERSISTENT_STORAGE_H

#include "config/paths_config.h"
#include "compat/compat.h"

// =============================================================================
// PERSISTENT STORAGE CLASS
// =============================================================================
// Handles LittleFS file operations and WiFi credential storage
// Merges LittleFS operations with credential management
// =============================================================================

struct WiFiCredentials {
  String ssid;
  String password;
  String ip;
  String gateway;
  bool useDHCP = false; // Default false for backward compatibility

  bool isEmpty() const {
    // Only require IP if not using DHCP
    return ssid.isEmpty() || (!useDHCP && ip.isEmpty());
  }
};

class PersistentStorage {
private:
  bool mounted = false;

  // Generic file operations
  String readFile(const char *path);
  void writeFile(const char *path, const char *content);

  // NVS helpers for WiFi credentials
  void saveCredentialsToNVS(const WiFiCredentials &creds);
  WiFiCredentials loadCredentialsFromNVS();
  void clearNVS();

public:
  // Initialize LittleFS
  bool begin();

  // Check if LittleFS is mounted
  bool isMounted() const { return mounted; }

  // Load WiFi credentials from LittleFS
  WiFiCredentials loadCredentials();

  // Save WiFi credentials to LittleFS
  void saveCredentials(const WiFiCredentials &creds);

  // Erase all credential files
  void eraseCredentials();

  // Load temperature offsets
  void loadTempOffsets(int &pitOffset, int &meatOffset);

  // Save temperature offsets
  void saveTempOffsets(int pitOffset, int meatOffset);

  // Load PID tuning parameters
  void loadPIDTunings(double &kp, double &ki, double &kd);

  // Save PID tuning parameters
  void savePIDTunings(double kp, double ki, double kd);

  // Load Meat Setpoint
  double loadMeatSetpoint();

  // Save Meat Setpoint
  void saveMeatSetpoint(double meatSetpoint);

  // Load Keep Warm Setpoint
  double loadKeepWarmSetpoint();

  // Save Keep Warm Setpoint
  void saveKeepWarmSetpoint(double keepWarmSetpoint);

  // Load timezone preference
  String loadTimezone();

  // Save timezone preference
  void saveTimezone(const String &tz);

  // Load firmware version (OTA)
  String loadFirmwareVersion();

  // Save firmware version (OTA)
  void saveFirmwareVersion(const String &version);

  // Debug: List all files in LittleFS
  void listAllFiles();
};

#endif // PERSISTENT_STORAGE_H
