#include "persistent_storage.h"
#include "config/control_config.h"
#include "config/network_config.h"
#include "compat/compat.h"
#include <esp_littlefs.h>
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

// =============================================================================
// PERSISTENT STORAGE IMPLEMENTATION
// =============================================================================

namespace {
constexpr const char *kBasePath = "/littlefs";

std::string buildPath(const char *path) {
  if (!path) {
    return std::string(kBasePath);
  }
  if (path[0] == '/') {
    return std::string(kBasePath) + path;
  }
  return std::string(kBasePath) + "/" + path;
}

bool nvsSetString(nvs_handle_t handle, const char *key, const String &value) {
  return nvs_set_str(handle, key, value.c_str()) == ESP_OK;
}

String nvsGetString(nvs_handle_t handle, const char *key) {
  size_t required = 0;
  if (nvs_get_str(handle, key, nullptr, &required) != ESP_OK || required == 0) {
    return String();
  }
  std::string buffer(required, '\0');
  if (nvs_get_str(handle, key, buffer.data(), &required) != ESP_OK) {
    return String();
  }
  buffer.resize(required - 1);
  return String(buffer);
}
} // namespace

bool PersistentStorage::begin() {
  esp_err_t nvsInit = nvs_flash_init();
  if (nvsInit == ESP_ERR_NVS_NO_FREE_PAGES || nvsInit == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvsInit = nvs_flash_init();
  }
  if (nvsInit != ESP_OK) {
    Serial.println("[ERROR] NVS init failed");
    mounted = false;
    return false;
  }

  esp_vfs_littlefs_conf_t conf = {
      .base_path = kBasePath,
      .partition_label = "littlefs",
      .partition = nullptr,
      .format_if_mount_failed = true,
      .read_only = false,
      .dont_mount = false,
      .grow_on_mount = true,
  };

  esp_err_t ret = esp_vfs_littlefs_register(&conf);
  if (ret != ESP_OK) {
    Serial.println("[ERROR] LittleFS mount failed");
    mounted = false;
    return false;
  }

  Serial.println("[OK] LittleFS mounted successfully");
  mounted = true;
  return true;
}

String PersistentStorage::readFile(const char *path) {
  if (!mounted) {
    Serial.println("[ERROR] LittleFS not mounted");
    return String();
  }

  std::string fullPath = buildPath(path);
  FILE *file = fopen(fullPath.c_str(), "r");
  if (!file) {
    Serial.printf("  [WARN] Failed to open file: %s\n", fullPath.c_str());
    return String();
  }

  char buffer[256];
  if (!fgets(buffer, sizeof(buffer), file)) {
    fclose(file);
    return String();
  }
  fclose(file);

  std::string content(buffer);
  if (!content.empty() && (content.back() == '\n' || content.back() == '\r')) {
    content.erase(content.find_last_not_of("\r\n") + 1);
  }

  Serial.printf("  [OK] Read: %s\n", content.c_str());
  return String(content);
}

void PersistentStorage::writeFile(const char *path, const char *content) {
  if (!mounted) {
    Serial.println("[ERROR] LittleFS not mounted");
    return;
  }

  std::string fullPath = buildPath(path);
  FILE *file = fopen(fullPath.c_str(), "w");
  if (!file) {
    Serial.printf("  [ERROR] Failed to open file for writing: %s\n", fullPath.c_str());
    return;
  }

  if (content) {
    fprintf(file, "%s", content);
  }
  fclose(file);
}

WiFiCredentials PersistentStorage::loadCredentials() {
  WiFiCredentials creds;
  creds.ssid = readFile(PATH_SSID);
  creds.password = readFile(PATH_PASS);
  creds.ip = readFile(PATH_IP);
  creds.gateway = readFile(PATH_GATEWAY);

  String dhcpStr = readFile(PATH_USE_DHCP);
  if (dhcpStr.length() == 0) {
    creds.useDHCP = true;
  } else {
    creds.useDHCP = (dhcpStr == "true" || dhcpStr == "1");
  }

  if (creds.ssid.isEmpty()) {
    WiFiCredentials nvsCreds = loadCredentialsFromNVS();
    if (!nvsCreds.ssid.isEmpty()) {
      Serial.println("[OK] Restoring WiFi credentials from NVS");
      saveCredentials(nvsCreds);
      creds = nvsCreds;
    }
  }

  Serial.println("\nLoaded WiFi configuration:");
  Serial.printf("  SSID: %s\n", creds.ssid.c_str());
  Serial.printf("  IP: %s\n", creds.ip.c_str());
  Serial.printf("  Gateway: %s\n", creds.gateway.c_str());
  Serial.printf("  IP Mode: %s\n", creds.useDHCP ? "DHCP" : "Static");

  if (!creds.useDHCP && creds.ip.isEmpty()) {
    Serial.println("[WARN] Static IP requested but IP is empty. Falling back to DHCP.");
    creds.useDHCP = true;
  }

  return creds;
}

void PersistentStorage::saveCredentials(const WiFiCredentials &creds) {
  Serial.println("\nSaving WiFi credentials...");
  writeFile(PATH_SSID, creds.ssid.c_str());
  writeFile(PATH_PASS, creds.password.c_str());
  writeFile(PATH_IP, creds.ip.c_str());
  writeFile(PATH_GATEWAY, creds.gateway.c_str());
  writeFile(PATH_USE_DHCP, creds.useDHCP ? "true" : "false");
  saveCredentialsToNVS(creds);
  Serial.printf("[OK] Credentials saved (Mode: %s)\n",
                creds.useDHCP ? "DHCP" : "Static");
}

void PersistentStorage::eraseCredentials() {
  Serial.println("\n===========================================");
  Serial.println("ERASING ALL CONFIGURATION FILES");
  Serial.println("===========================================");

  if (!mounted) {
    Serial.println("[ERROR] LittleFS not mounted");
    return;
  }

  Serial.println("Step 1: Overwriting known config files...");
  writeFile(PATH_SSID, "");
  writeFile(PATH_PASS, "");
  writeFile(PATH_IP, "");
  writeFile(PATH_GATEWAY, "");
  writeFile(PATH_USE_DHCP, "");

  Serial.println("Step 2: Deleting filesystem objects...");
  bool clean = false;
  int pass = 0;

  while (!clean && pass < 50) {
    pass++;
    clean = true;

    DIR *dir = opendir(kBasePath);
    if (!dir) {
      Serial.println("[ERROR] Failed to open root directory");
      break;
    }

    struct dirent *entry = nullptr;
    String fileToDelete = "";

    while ((entry = readdir(dir)) != nullptr) {
      String fileName = String(entry->d_name);
      if (!fileName.startsWith("/")) {
        fileName = "/" + fileName;
      }

      bool isWebResource =
          (fileName.endsWith(".html") || fileName.endsWith(".css") ||
           fileName.endsWith(".js") || fileName.endsWith(".png") ||
           fileName.endsWith(".ico"));

      if (!isWebResource) {
        fileToDelete = fileName;
        clean = false;
        break;
      }
    }

    closedir(dir);

    if (fileToDelete.length() > 0) {
      std::string fullPath = buildPath(fileToDelete.c_str());
      Serial.printf("  [DELETE] %s ... ", fullPath.c_str());
      if (unlink(fullPath.c_str()) == 0) {
        Serial.println("OK");
      } else {
        Serial.println("FAILED (Content was cleared in Step 1)");
        Serial.println("  [WARN] Skipping undeletable file.");
        break;
      }
    }
  }

  Serial.println("\n[OK] Erase sequence complete.");
  Serial.println("Erasing SDK internal NVS credentials...");
  clearNVS();
  esp_wifi_restore();
  delay(500);

  Serial.println("===========================================\n");
}

void PersistentStorage::saveCredentialsToNVS(const WiFiCredentials &creds) {
  nvs_handle_t handle = 0;
  if (nvs_open("wifi", NVS_READWRITE, &handle) != ESP_OK) {
    Serial.println("[ERROR] Failed to open NVS for write");
    return;
  }

  nvsSetString(handle, "ssid", creds.ssid);
  nvsSetString(handle, "pass", creds.password);
  nvsSetString(handle, "ip", creds.ip);
  nvsSetString(handle, "gw", creds.gateway);
  nvs_set_u8(handle, "useDHCP", creds.useDHCP ? 1 : 0);
  nvsSetString(handle, "marker", String("valid"));
  nvs_commit(handle);
  nvs_close(handle);
  Serial.println("[OK] Credentials saved to NVS");
}

WiFiCredentials PersistentStorage::loadCredentialsFromNVS() {
  WiFiCredentials creds;
  nvs_handle_t handle = 0;
  if (nvs_open("wifi", NVS_READONLY, &handle) != ESP_OK) {
    return creds;
  }

  creds.ssid = nvsGetString(handle, "ssid");
  creds.password = nvsGetString(handle, "pass");
  creds.ip = nvsGetString(handle, "ip");
  creds.gateway = nvsGetString(handle, "gw");
  uint8_t useDHCP = 0;
  nvs_get_u8(handle, "useDHCP", &useDHCP);
  creds.useDHCP = useDHCP == 1;

  if (!creds.useDHCP && creds.ip.isEmpty()) {
    creds.useDHCP = true;
  }

  String marker = nvsGetString(handle, "marker");
  if (marker != "valid") {
    Serial.println("[WARN] NVS credentials invalid or empty");
    creds.ssid = "";
  }

  nvs_close(handle);
  return creds;
}

void PersistentStorage::clearNVS() {
  nvs_handle_t handle = 0;
  if (nvs_open("wifi", NVS_READWRITE, &handle) == ESP_OK) {
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
  }
  Serial.println("[OK] NVS cleared");
}

String PersistentStorage::loadFirmwareVersion() {
  nvs_handle_t handle = 0;
  if (nvs_open("wifi", NVS_READONLY, &handle) != ESP_OK) {
    return String();
  }

  String version = nvsGetString(handle, "fwVersion");
  if (version.length() > 0) {
    Serial.printf("[OK] Loaded firmware version from NVS: %s\n",
                  version.c_str());
  }
  nvs_close(handle);
  return version;
}

void PersistentStorage::saveFirmwareVersion(const String &version) {
  if (version.length() == 0) {
    return;
  }

  nvs_handle_t handle = 0;
  if (nvs_open("wifi", NVS_READWRITE, &handle) != ESP_OK) {
    return;
  }
  nvsSetString(handle, "fwVersion", version);
  nvs_commit(handle);
  nvs_close(handle);
  Serial.printf("[OK] Saved firmware version to NVS: %s\n", version.c_str());
}

void PersistentStorage::loadTempOffsets(int &pitOffset, int &meatOffset) {
  String pitStr = readFile(PATH_PIT_OFFSET);
  String meatStr = readFile(PATH_MEAT_OFFSET);

  pitOffset = (pitStr.length() > 0) ? pitStr.toInt() : 0;
  meatOffset = (meatStr.length() > 0) ? meatStr.toInt() : 0;

  Serial.printf("Loaded Calibration Offsets - Pit: %d, Meat: %d\n", pitOffset,
                meatOffset);
}

void PersistentStorage::saveTempOffsets(int pitOffset, int meatOffset) {
  writeFile(PATH_PIT_OFFSET, String(pitOffset).c_str());
  writeFile(PATH_MEAT_OFFSET, String(meatOffset).c_str());
  Serial.printf("[OK] Saved Offsets - Pit: %d, Meat: %d\n", pitOffset,
                meatOffset);
}

void PersistentStorage::loadPIDTunings(double &kp, double &ki, double &kd) {
  String kpStr = readFile(PATH_PID_KP);
  String kiStr = readFile(PATH_PID_KI);
  String kdStr = readFile(PATH_PID_KD);

  kp = (kpStr.length() > 0) ? kpStr.toDouble() : PID_KP;
  ki = (kiStr.length() > 0) ? kiStr.toDouble() : PID_KI;
  kd = (kdStr.length() > 0) ? kdStr.toDouble() : PID_KD;

  Serial.printf("Loaded PID Tunings - Kp: %.2f, Ki: %.2f, Kd: %.2f\n", kp, ki,
                kd);
}

void PersistentStorage::savePIDTunings(double kp, double ki, double kd) {
  writeFile(PATH_PID_KP, String(kp, 2).c_str());
  writeFile(PATH_PID_KI, String(ki, 2).c_str());
  writeFile(PATH_PID_KD, String(kd, 2).c_str());
  Serial.printf("[OK] Saved PID Tunings - Kp: %.2f, Ki: %.2f, Kd: %.2f\n", kp,
                ki, kd);
}

double PersistentStorage::loadMeatSetpoint() {
  String meatSetpointStr = readFile(PATH_MEAT_SETPOINT);
  return (meatSetpointStr.length() > 0) ? meatSetpointStr.toDouble() : 195.0;
}

void PersistentStorage::saveMeatSetpoint(double meatSetpoint) {
  writeFile(PATH_MEAT_SETPOINT, String(meatSetpoint, 1).c_str());
  Serial.printf("[OK] Saved Meat Setpoint: %.1f\n", meatSetpoint);
}

double PersistentStorage::loadKeepWarmSetpoint() {
  String kwSetpointStr = readFile(PATH_KEEP_WARM_SETPOINT);
  return (kwSetpointStr.length() > 0) ? kwSetpointStr.toDouble() : 160.0;
}

void PersistentStorage::saveKeepWarmSetpoint(double keepWarmSetpoint) {
  writeFile(PATH_KEEP_WARM_SETPOINT, String(keepWarmSetpoint, 1).c_str());
  Serial.printf("[OK] Saved Keep Warm Setpoint: %.1f\n", keepWarmSetpoint);
}

String PersistentStorage::loadTimezone() {
  String tz = readFile(PATH_TIMEZONE);
  if (tz.length() == 0) {
    return CURRENT_TIMEZONE;
  }
  return tz;
}

void PersistentStorage::saveTimezone(const String &tz) {
  writeFile(PATH_TIMEZONE, tz.c_str());
  Serial.printf("[OK] Saved Timezone: %s\n", tz.c_str());
}

void PersistentStorage::listAllFiles() {
  if (!mounted) {
    Serial.println("[ERROR] LittleFS not mounted");
    return;
  }

  Serial.println("\nLittleFS files:");
  DIR *dir = opendir(kBasePath);
  if (!dir) {
    Serial.println("[ERROR] Failed to open root directory");
    return;
  }

  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    std::string fullPath = std::string(kBasePath) + "/" + entry->d_name;
    struct stat st;
    if (stat(fullPath.c_str(), &st) == 0) {
      Serial.printf("  FILE: %s (%ld bytes)\n", entry->d_name,
                    static_cast<long>(st.st_size));
    }
  }
  closedir(dir);
}
