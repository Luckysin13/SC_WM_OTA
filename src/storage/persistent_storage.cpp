#include "persistent_storage.h"
#include "config/control_config.h"
#include "config/network_config.h"
#include "compat/compat.h"
#include <esp_littlefs.h>
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// =============================================================================
// PERSISTENT STORAGE IMPLEMENTATION
// =============================================================================

namespace {
constexpr const char *kBasePath = "/littlefs";
constexpr const char *kConfigNamespace = "config";

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

std::string buildTempPath(const char *path) {
  return buildPath(path) + ".tmp";
}

void trimTrailingNewlines(std::string &content) {
  while (!content.empty() &&
         (content.back() == '\n' || content.back() == '\r')) {
    content.pop_back();
  }
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
      .format_if_mount_failed = false,
      .read_only = false,
      .dont_mount = false,
      .grow_on_mount = true,
  };

  esp_err_t ret = esp_vfs_littlefs_register(&conf);
  if (ret != ESP_OK) {
    Serial.printf("[ERROR] LittleFS mount failed: %s\n", esp_err_to_name(ret));
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

  std::string content;
  char buffer[256];
  size_t bytesRead = 0;
  while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    content.append(buffer, bytesRead);
  }
  if (ferror(file)) {
    fclose(file);
    return String();
  }
  fclose(file);

  trimTrailingNewlines(content);

  Serial.printf("  [OK] Read: %s\n", content.c_str());
  return String(content);
}

bool PersistentStorage::writeFile(const char *path, const char *content) {
  if (!mounted) {
    Serial.println("[ERROR] LittleFS not mounted");
    return false;
  }

  std::string fullPath = buildPath(path);
  std::string tempPath = buildTempPath(path);
  FILE *file = fopen(tempPath.c_str(), "w");
  if (!file) {
    Serial.printf("  [ERROR] Failed to open file for writing: %s\n", tempPath.c_str());
    return false;
  }

  size_t contentLength = content ? strlen(content) : 0;
  if (contentLength > 0 && fwrite(content, 1, contentLength, file) != contentLength) {
    fclose(file);
    unlink(tempPath.c_str());
    Serial.printf("  [ERROR] Failed to write file: %s\n", tempPath.c_str());
    return false;
  }

  fflush(file);
  fsync(fileno(file));
  fclose(file);

  if (rename(tempPath.c_str(), fullPath.c_str()) != 0) {
    unlink(tempPath.c_str());
    Serial.printf("  [ERROR] Failed to replace file: %s\n", fullPath.c_str());
    return false;
  }

  return true;
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

bool PersistentStorage::savePIDTuningsToNVS(double kp, double ki, double kd) {
  nvs_handle_t handle = 0;
  if (nvs_open(kConfigNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    Serial.println("[WARN] Failed to open config NVS for PID write");
    return false;
  }

  bool ok = nvsSetString(handle, "pid_kp", String(kp, 4)) &&
            nvsSetString(handle, "pid_ki", String(ki, 4)) &&
            nvsSetString(handle, "pid_kd", String(kd, 4)) &&
            nvsSetString(handle, "pid_marker", String("valid")) &&
            nvs_commit(handle) == ESP_OK;

  nvs_close(handle);

  if (!ok) {
    Serial.println("[WARN] Failed to save PID tunings to NVS");
  }
  return ok;
}

bool PersistentStorage::saveTempOffsetsToNVS(int pitOffset, int meatOffset) {
  nvs_handle_t handle = 0;
  if (nvs_open(kConfigNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    Serial.println("[WARN] Failed to open config NVS for offset write");
    return false;
  }

  bool ok = nvsSetString(handle, "pit_offset", String(pitOffset)) &&
            nvsSetString(handle, "meat_offset", String(meatOffset)) &&
            nvsSetString(handle, "offset_marker", String("valid")) &&
            nvs_commit(handle) == ESP_OK;

  nvs_close(handle);

  if (!ok) {
    Serial.println("[WARN] Failed to save probe calibration offsets to NVS");
  }
  return ok;
}

bool PersistentStorage::loadTempOffsetsFromNVS(int &pitOffset, int &meatOffset) {
  nvs_handle_t handle = 0;
  if (nvs_open(kConfigNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }

  String marker = nvsGetString(handle, "offset_marker");
  if (marker != "valid") {
    nvs_close(handle);
    return false;
  }

  String pitOffsetStr = nvsGetString(handle, "pit_offset");
  String meatOffsetStr = nvsGetString(handle, "meat_offset");
  nvs_close(handle);

  if (pitOffsetStr.length() == 0 || meatOffsetStr.length() == 0) {
    return false;
  }

  pitOffset = pitOffsetStr.toInt();
  meatOffset = meatOffsetStr.toInt();
  return true;
}

bool PersistentStorage::loadPIDTuningsFromNVS(double &kp, double &ki, double &kd) {
  nvs_handle_t handle = 0;
  if (nvs_open(kConfigNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }

  String marker = nvsGetString(handle, "pid_marker");
  if (marker != "valid") {
    nvs_close(handle);
    return false;
  }

  String kpStr = nvsGetString(handle, "pid_kp");
  String kiStr = nvsGetString(handle, "pid_ki");
  String kdStr = nvsGetString(handle, "pid_kd");
  nvs_close(handle);

  if (kpStr.length() == 0 || kiStr.length() == 0 || kdStr.length() == 0) {
    return false;
  }

  kp = kpStr.toDouble();
  ki = kiStr.toDouble();
  kd = kdStr.toDouble();
  return true;
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

  int filePitOffset = (pitStr.length() > 0) ? pitStr.toInt() : 0;
  int fileMeatOffset = (meatStr.length() > 0) ? meatStr.toInt() : 0;

  if (loadTempOffsetsFromNVS(pitOffset, meatOffset)) {
    if (pitStr != String(pitOffset)) {
      writeFile(PATH_PIT_OFFSET, String(pitOffset).c_str());
    }
    if (meatStr != String(meatOffset)) {
      writeFile(PATH_MEAT_OFFSET, String(meatOffset).c_str());
    }
  } else {
    pitOffset = filePitOffset;
    meatOffset = fileMeatOffset;
    saveTempOffsetsToNVS(pitOffset, meatOffset);
  }

  Serial.printf("Loaded Calibration Offsets - Pit: %d, Meat: %d\n", pitOffset,
                meatOffset);
}

void PersistentStorage::saveTempOffsets(int pitOffset, int meatOffset) {
  bool filesOk = true;
  filesOk = writeFile(PATH_PIT_OFFSET, String(pitOffset).c_str()) && filesOk;
  filesOk = writeFile(PATH_MEAT_OFFSET, String(meatOffset).c_str()) && filesOk;

  bool nvsOk = saveTempOffsetsToNVS(pitOffset, meatOffset);

  Serial.printf("[OK] Saved Offsets - Pit: %d, Meat: %d\n", pitOffset,
                meatOffset);
  if (!filesOk || !nvsOk) {
    Serial.printf("[WARN] Offset persistence partial failure (files=%s, nvs=%s)\n",
                  filesOk ? "ok" : "failed", nvsOk ? "ok" : "failed");
  }
}

void PersistentStorage::loadPIDTunings(double &kp, double &ki, double &kd) {
  String kpStr = readFile(PATH_PID_KP);
  String kiStr = readFile(PATH_PID_KI);
  String kdStr = readFile(PATH_PID_KD);

  double fileKp = (kpStr.length() > 0) ? kpStr.toDouble() : PID_KP;
  double fileKi = (kiStr.length() > 0) ? kiStr.toDouble() : PID_KI;
  double fileKd = (kdStr.length() > 0) ? kdStr.toDouble() : PID_KD;

  if (loadPIDTuningsFromNVS(kp, ki, kd)) {
    if (kpStr != String(kp, 2)) {
      writeFile(PATH_PID_KP, String(kp, 2).c_str());
    }
    if (kiStr != String(ki, 2)) {
      writeFile(PATH_PID_KI, String(ki, 2).c_str());
    }
    if (kdStr != String(kd, 2)) {
      writeFile(PATH_PID_KD, String(kd, 2).c_str());
    }
  } else {
    kp = fileKp;
    ki = fileKi;
    kd = fileKd;
    savePIDTuningsToNVS(kp, ki, kd);
  }

  Serial.printf("Loaded PID Tunings - Kp: %.2f, Ki: %.2f, Kd: %.2f\n", kp, ki,
                kd);
}

void PersistentStorage::savePIDTunings(double kp, double ki, double kd) {
  bool filesOk = true;
  filesOk = writeFile(PATH_PID_KP, String(kp, 2).c_str()) && filesOk;
  filesOk = writeFile(PATH_PID_KI, String(ki, 2).c_str()) && filesOk;
  filesOk = writeFile(PATH_PID_KD, String(kd, 2).c_str()) && filesOk;

  bool nvsOk = savePIDTuningsToNVS(kp, ki, kd);
  Serial.printf("[OK] Saved PID Tunings - Kp: %.2f, Ki: %.2f, Kd: %.2f\n", kp,
                ki, kd);
  if (!filesOk || !nvsOk) {
    Serial.printf("[WARN] PID persistence partial failure (files=%s, nvs=%s)\n",
                  filesOk ? "ok" : "failed", nvsOk ? "ok" : "failed");
  }
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

bool PersistentStorage::saveHistorySnapshot(const std::string &data) {
  if (!mounted) {
    Serial.println("[ERROR] LittleFS not mounted");
    return false;
  }

  std::string fullPath = buildPath(PATH_HISTORY_SNAPSHOT);
  std::string tempPath = buildTempPath(PATH_HISTORY_SNAPSHOT);

  FILE *file = fopen(tempPath.c_str(), "wb");
  if (!file) {
    Serial.printf("[ERROR] Failed to open history snapshot for writing: %s\n",
                  tempPath.c_str());
    return false;
  }

  if (!data.empty() &&
      fwrite(data.data(), 1, data.size(), file) != data.size()) {
    fclose(file);
    unlink(tempPath.c_str());
    Serial.println("[ERROR] Failed to write history snapshot");
    return false;
  }

  fflush(file);
  fsync(fileno(file));
  fclose(file);

  if (rename(tempPath.c_str(), fullPath.c_str()) != 0) {
    unlink(tempPath.c_str());
    Serial.println("[ERROR] Failed to replace history snapshot");
    return false;
  }

  Serial.printf("[OK] Saved history snapshot (%u bytes)\n",
                static_cast<unsigned>(data.size()));
  return true;
}

std::string PersistentStorage::loadHistorySnapshot() {
  if (!mounted) {
    Serial.println("[ERROR] LittleFS not mounted");
    return std::string();
  }

  std::string fullPath = buildPath(PATH_HISTORY_SNAPSHOT);
  FILE *file = fopen(fullPath.c_str(), "rb");
  if (!file) {
    return std::string();
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return std::string();
  }

  long fileSize = ftell(file);
  if (fileSize < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return std::string();
  }

  std::string data(static_cast<size_t>(fileSize), '\0');
  if (!data.empty() && fread(data.data(), 1, data.size(), file) != data.size()) {
    fclose(file);
    return std::string();
  }

  fclose(file);
  Serial.printf("[OK] Loaded history snapshot (%u bytes)\n",
                static_cast<unsigned>(data.size()));
  return data;
}

void PersistentStorage::clearHistorySnapshot() {
  if (!mounted) {
    return;
  }

  std::string fullPath = buildPath(PATH_HISTORY_SNAPSHOT);
  if (unlink(fullPath.c_str()) == 0) {
    Serial.println("[OK] Cleared history snapshot");
  }
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
