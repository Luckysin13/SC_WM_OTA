#include "ota_updater.h"
#include "compat/compat.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_efuse.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <cstring>
#include <cstdlib>
#include <vector>

// =============================================================================
// OTA UPDATER IMPLEMENTATION (ESP-IDF)
// =============================================================================

namespace {
static const char kOtaPinnedCertPem[] = "";

void configureHttpClient(esp_http_client_config_t &config, const char *url,
                         int timeoutMs) {
  memset(&config, 0, sizeof(config));
  config.url = url;
  config.timeout_ms = timeoutMs;
  if (strlen(kOtaPinnedCertPem) > 0) {
    config.cert_pem = kOtaPinnedCertPem;
  } else {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }
}

bool httpGetString(const String &url, std::string &out) {
  esp_http_client_config_t config = {};
  configureHttpClient(config, url.c_str(), 10000);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return false;
  }

  esp_http_client_set_header(client, "Accept-Encoding", "identity");

  if (esp_http_client_open(client, 0) != ESP_OK) {
    esp_http_client_cleanup(client);
    return false;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);
  Serial.printf("[OTA] HTTP status: %d, content-length: %d\n",
                status_code, content_length);
  if (status_code < 200 || status_code >= 300) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  out.clear();
  if (content_length > 0) {
    out.reserve(content_length);
  } else {
    out.reserve(1024);
  }
  char buffer[512];
  int read = 0;
  while ((read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0) {
    out.append(buffer, read);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return !out.empty();
}
} // namespace

void OTAUpdater::begin() {
  Serial.println("[OTA] Updater initialized");
  esp_app_desc_t running_app_desc = {};
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running && esp_ota_get_partition_description(running, &running_app_desc) == ESP_OK) {
    String runningVersion = String(running_app_desc.version);
    if (runningVersion.length() > 0 && runningVersion != currentVersionOverride) {
      if (currentVersionOverride.length() > 0) {
        Serial.printf("[OTA] Overriding stored version %s with running %s\n",
                      currentVersionOverride.c_str(), runningVersion.c_str());
      }
      currentVersionOverride = runningVersion;
    }
  }
  String currentVersion = getCurrentVersion();
  Serial.printf("[OTA] Current version: %s\n", currentVersion.c_str());
  Serial.printf("[OTA] Manifest URL: %s\n", MANIFEST_URL);
}

void OTAUpdater::checkForUpdates() {
  if (status == CHECKING || status == DOWNLOADING || status == INSTALLING) {
    return;
  }

  availableVersion = "";
  firmwareUrl = "";
  spiffsUrl = "";
  availableSecureVersion = 0;
  updateError = "";

  status = CHECKING;
  Serial.println("[OTA] Checking for updates...");

  String newVersion = "";
  String description = "";
  String firmwareName = "";
  String spiffsName = "";
  uint32_t secureVersion = 0;

  if (fetchManifest(newVersion, description, firmwareName, spiffsName,
                    secureVersion)) {
    String currentVersion = getCurrentVersion();
    if (newVersion != currentVersion) {
      availableVersion = newVersion;
      firmwareUrl = firmwareName;
      spiffsUrl = spiffsName;
      availableSecureVersion = secureVersion;
      status = UPDATE_AVAILABLE;
      Serial.printf("[OTA] Update available: %s -> %s\n", currentVersion.c_str(),
                    newVersion.c_str());
      Serial.printf("[OTA] Description: %s\n", description.c_str());
    } else {
      status = NO_UPDATE;
      Serial.println("[OTA] Already on latest version");
    }
  } else {
    status = FAILED;
    updateError = "Failed to fetch manifest";
    Serial.println("[OTA] Failed to check for updates");
  }
}

bool OTAUpdater::startUpdate() {
  if (status != UPDATE_AVAILABLE) {
    updateError = "No update available or update in progress";
    return false;
  }

  status = DOWNLOADING;
  updateProgress = 0;
  spiffsProgress = 0;
  firmwareProgress = 0;
  phase = PHASE_NONE;
  Serial.println("[OTA] Starting firmware update...");

  if (spiffsUrl.length() > 0) {
    Serial.println("[OTA] Updating LittleFS...");
    if (!downloadAndUpdateSpiffs(spiffsUrl)) {
      status = FAILED;
      Serial.printf("[OTA] LittleFS update failed: %s\n", updateError.c_str());
      return false;
    }
  }

  if (firmwareUrl.length() == 0) {
    firmwareUrl = FIRMWARE_URL;
  }

  if (availableSecureVersion > 0) {
    esp_app_desc_t running_app_desc = {};
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_partition_description(running, &running_app_desc) == ESP_OK) {
      if (availableSecureVersion <= running_app_desc.secure_version) {
        updateError = "Secure version rollback blocked";
        return false;
      }
    }
    if (!esp_efuse_check_secure_version(availableSecureVersion)) {
      updateError = "Secure version not allowed by eFuse";
      return false;
    }
  }

  if (downloadAndUpdate(firmwareUrl)) {
    status = SUCCESS;
    updateProgress = 100;
    phase = PHASE_NONE;
    Serial.println("[OTA] Update successful! Rebooting in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return true;  // Won't reach here
  }

  status = FAILED;
  phase = PHASE_NONE;
  Serial.printf("[OTA] Update failed: %s\n", updateError.c_str());
  return false;
}

void OTAUpdater::cancelUpdate() {
  if (status == DOWNLOADING || status == INSTALLING) {
    status = IDLE;
    updateProgress = 0;
    Serial.println("[OTA] Update cancelled");
  }
}

void OTAUpdater::update() {}

bool OTAUpdater::fetchManifest(String &outVersion, String &outDescription,
                               String &outFirmware, String &outSpiffs,
                               uint32_t &outSecureVersion) {
  outSecureVersion = 0;
  String manifestUrl = String(MANIFEST_URL);
  manifestUrl += (manifestUrl.indexOf('?') >= 0 ? "&" : "?");
  manifestUrl += "t=" + String(static_cast<unsigned long>(millis()));

  Serial.printf("[OTA] Fetching manifest from: %s\n", manifestUrl.c_str());

  std::string payload;
  if (!httpGetString(manifestUrl, payload)) {
    return false;
  }

  String payloadStr(payload);

  int versionStart = payloadStr.indexOf("\"version\":");
  if (versionStart == -1) {
    Serial.println("[OTA] Invalid manifest format");
    return false;
  }

  versionStart = payloadStr.indexOf("\"", versionStart + 10);
  int versionEnd = payloadStr.indexOf("\"", versionStart + 1);
  if (versionStart == -1 || versionEnd == -1) {
    return false;
  }
  outVersion = payloadStr.substring(versionStart + 1, versionEnd);

  int descStart = payloadStr.indexOf("\"description\":");
  if (descStart != -1) {
    descStart = payloadStr.indexOf("\"", descStart + 14);
    int descEnd = payloadStr.indexOf("\"", descStart + 1);
    if (descStart != -1 && descEnd != -1) {
      outDescription = payloadStr.substring(descStart + 1, descEnd);
    }
  }

  int fwStart = payloadStr.indexOf("\"firmware\":");
  if (fwStart != -1) {
    fwStart = payloadStr.indexOf("\"", fwStart + 11);
    int fwEnd = payloadStr.indexOf("\"", fwStart + 1);
    if (fwStart != -1 && fwEnd != -1) {
      outFirmware = payloadStr.substring(fwStart + 1, fwEnd);
    }
  }

  int spStart = payloadStr.indexOf("\"spiffs\":");
  if (spStart != -1) {
    spStart = payloadStr.indexOf("\"", spStart + 9);
    int spEnd = payloadStr.indexOf("\"", spStart + 1);
    if (spStart != -1 && spEnd != -1) {
      outSpiffs = payloadStr.substring(spStart + 1, spEnd);
    }
  }

  int svStart = payloadStr.indexOf("\"secure_version\":");
  if (svStart != -1) {
    svStart = payloadStr.indexOf(":", svStart);
    if (svStart != -1) {
      int svEnd = payloadStr.indexOf(",", svStart + 1);
      if (svEnd == -1) {
        svEnd = payloadStr.indexOf("}", svStart + 1);
      }
      if (svEnd != -1) {
        String svStr = payloadStr.substring(svStart + 1, svEnd);
        outSecureVersion = static_cast<uint32_t>(strtoul(svStr.c_str(), nullptr, 10));
      }
    }
  }

  int lastSlash = String(MANIFEST_URL).lastIndexOf('/');
  if (lastSlash != -1) {
    String baseUrl = String(MANIFEST_URL).substring(0, lastSlash + 1);
    if (outFirmware.length() > 0) {
      outFirmware = baseUrl + outFirmware;
    }
    if (outSpiffs.length() > 0) {
      outSpiffs = baseUrl + outSpiffs;
    }
  }

  return true;
}

bool OTAUpdater::downloadAndUpdate(const String &firmwareUrl) {
  Serial.printf("[OTA] Downloading firmware from: %s\n", firmwareUrl.c_str());

  String downloadUrl = firmwareUrl;
  downloadUrl += (downloadUrl.indexOf('?') >= 0 ? "&" : "?");
  downloadUrl += "t=" + String(static_cast<unsigned long>(millis()));

    esp_http_client_config_t config = {};
    configureHttpClient(config, downloadUrl.c_str(), 15000);

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &config;
    ota_config.partial_http_download = true;  // Allow partial downloads
    ota_config.max_http_request_size = 8192;  // Chunk size

  esp_https_ota_handle_t ota_handle = nullptr;
  esp_err_t ret = esp_https_ota_begin(&ota_config, &ota_handle);
  if (ret != ESP_OK) {
    Serial.printf("[OTA] Begin failed: %s\n", esp_err_to_name(ret));
    updateError = "OTA begin failed: " + String(esp_err_to_name(ret));
    return false;
  }

  esp_app_desc_t new_app_desc = {};
  if (esp_https_ota_get_img_desc(ota_handle, &new_app_desc) == ESP_OK) {
    Serial.printf("[OTA] New firmware version: %s\n", new_app_desc.version);
    // Log version info but don't block - manifest version is authoritative
    if (availableVersion.length() > 0 &&
        String(new_app_desc.version) != availableVersion) {
      Serial.printf("[OTA] Warning: image version %s differs from manifest %s\n",
                    new_app_desc.version, availableVersion.c_str());
    }
  }

  phase = PHASE_FIRMWARE;
  status = INSTALLING;

  size_t fallback_total = 0;
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(nullptr);
  if (update_partition) {
    fallback_total = update_partition->size;
    Serial.printf("[OTA] Target partition: %s, size: %d\n", update_partition->label, update_partition->size);
  }

  int image_size = esp_https_ota_get_image_size(ota_handle);
  Serial.printf("[OTA] Image size from header: %d bytes\n", image_size);

  while (true) {
    ret = esp_https_ota_perform(ota_handle);
    if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      int image_len = esp_https_ota_get_image_len_read(ota_handle);
      if (image_size > 0) {
        firmwareProgress = (image_len * 100) / image_size;
        updateProgress = firmwareProgress;
      } else if (fallback_total > 0) {
        int fallbackProgress = (image_len * 100) / static_cast<int>(fallback_total);
        if (fallbackProgress > 99) {
          fallbackProgress = 99;
        }
        firmwareProgress = fallbackProgress;
        updateProgress = firmwareProgress;
      }
      continue;
    }
    break;
  }

  int final_len = esp_https_ota_get_image_len_read(ota_handle);
  Serial.printf("[OTA] Download complete. Bytes read: %d, expected: %d\n", final_len, image_size);

  if (ret != ESP_OK) {
    Serial.printf("[OTA] Perform failed: %s\n", esp_err_to_name(ret));
    updateError = "OTA perform failed: " + String(esp_err_to_name(ret));
    esp_https_ota_abort(ota_handle);
    return false;
  }

  // Check if OTA data was completely received
  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    Serial.println("[OTA] ERROR: Complete data was not received!");
    updateError = "OTA incomplete - data not fully received";
    esp_https_ota_abort(ota_handle);
    return false;
  }

  Serial.println("[OTA] Data complete, finishing OTA...");
  esp_err_t finish_err = esp_https_ota_finish(ota_handle);
  if (finish_err != ESP_OK) {
    Serial.printf("[OTA] Finish failed: %s\n", esp_err_to_name(finish_err));
    if (finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
      updateError = "OTA validation failed - image may be unsigned or corrupted";
    } else {
      updateError = "OTA finish failed: " + String(esp_err_to_name(finish_err));
    }
    return false;
  }

  Serial.println("[OTA] Firmware update successful!");
  firmwareProgress = 100;
  updateProgress = 100;
  return true;
}

bool OTAUpdater::downloadAndUpdateSpiffs(const String &spiffsUrl) {
  Serial.printf("[OTA] Downloading LittleFS from: %s\n", spiffsUrl.c_str());

    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "littlefs");
  if (!partition) {
      Serial.println("[OTA] LittleFS partition not found, trying without name...");
    partition = esp_partition_find_first(
          ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, nullptr);
  }
  if (!partition) {
      Serial.println("[OTA] LittleFS partition not found, listing all partitions:");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (it != nullptr) {
      const esp_partition_t *p = esp_partition_get(it);
      Serial.printf("[OTA] Found partition: %s, type: %d, subtype: %d, offset: 0x%08x, size: 0x%08x\n",
                    p->label, p->type, p->subtype, p->address, p->size);
      it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
      updateError = "LittleFS partition not found";
    return false;
  }

  String downloadUrl = spiffsUrl;
  downloadUrl += (downloadUrl.indexOf('?') >= 0 ? "&" : "?");
  downloadUrl += "t=" + String(static_cast<unsigned long>(millis()));

  esp_http_client_config_t config = {};
  configureHttpClient(config, downloadUrl.c_str(), 15000);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    updateError = "HTTP client init failed";
    return false;
  }

  if (esp_http_client_open(client, 0) != ESP_OK) {
    updateError = "HTTP open failed";
    esp_http_client_cleanup(client);
    return false;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);
  char *contentEncoding = nullptr;
  if (esp_http_client_get_header(client, "Content-Encoding", &contentEncoding) != ESP_OK) {
    contentEncoding = nullptr;
  }
    Serial.printf("[OTA] LittleFS HTTP status: %d, content-length: %d, encoding: %s\n",
                status_code, content_length,
                contentEncoding ? contentEncoding : "identity");
  if (status_code < 200 || status_code >= 300) {
    updateError = "HTTP error";
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }
  if (contentEncoding != nullptr && strcasecmp(contentEncoding, "gzip") == 0) {
      updateError = "LittleFS download compressed";
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  if (esp_partition_erase_range(partition, 0, partition->size) != ESP_OK) {
    updateError = "Partition erase failed";
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  phase = PHASE_SPIFFS;
  status = DOWNLOADING;

  const size_t bufferSize = 1024;
  std::vector<uint8_t> buffer(bufferSize);
  size_t written = 0;
  int read = 0;
  size_t offset = 0;

  while ((read = esp_http_client_read(client,
                                      reinterpret_cast<char *>(buffer.data()),
                                      buffer.size())) > 0) {
    if (esp_partition_write(partition, offset, buffer.data(), read) != ESP_OK) {
      updateError = "Partition write failed";
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    offset += read;
    written += read;
    if (content_length > 0) {
      spiffsProgress = (written * 100) / content_length;
    } else if (partition->size > 0) {
      spiffsProgress = (written * 100) / partition->size;
      if (spiffsProgress > 99) {
        spiffsProgress = 99;
      }
    }
    updateProgress = spiffsProgress;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (written == 0) {
      updateError = "Empty LittleFS download";
    return false;
  }
  if (content_length > 0 && written != static_cast<size_t>(content_length)) {
      updateError = "LittleFS download incomplete";
    return false;
  }

  spiffsProgress = 100;
  updateProgress = 100;
  return true;
}

#if 0
#include "ota_updater.h"
#include <esp_task_wdt.h>

// =============================================================================
// OTA UPDATER IMPLEMENTATION
// =============================================================================

void OTAUpdater::begin() {
  Serial.println("[OTA] Updater initialized");
  String currentVersion = getCurrentVersion();
  Serial.printf("[OTA] Current version: %s\n", currentVersion.c_str());
  Serial.printf("[OTA] Manifest URL: %s\n", MANIFEST_URL);
}

void OTAUpdater::checkForUpdates() {
  if (status == CHECKING || status == DOWNLOADING || status == INSTALLING) {
    return; // Already checking or updating
  }

  // Reset cached values for a fresh check
  availableVersion = "";
  firmwareUrl = "";
  spiffsUrl = "";
  updateError = "";

  status = CHECKING;
  Serial.println("[OTA] Checking for updates...");

  String newVersion = "";
  String description = "";
  String firmwareName = "";
  String spiffsName = "";

  if (fetchManifest(newVersion, description, firmwareName, spiffsName)) {
    String currentVersion = getCurrentVersion();
    if (newVersion != currentVersion) {
      availableVersion = newVersion;
      firmwareUrl = firmwareName;
      spiffsUrl = spiffsName;
      status = UPDATE_AVAILABLE;
      Serial.printf("[OTA] Update available: %s -> %s\n", currentVersion.c_str(),
                    newVersion.c_str());
      Serial.printf("[OTA] Description: %s\n", description.c_str());
    } else {
      status = NO_UPDATE;
      Serial.println("[OTA] Already on latest version");
    }
  } else {
    status = FAILED;
    updateError = "Failed to fetch manifest";
    Serial.println("[OTA] Failed to check for updates");
  }
}

bool OTAUpdater::startUpdate() {
  if (status != UPDATE_AVAILABLE) {
    updateError = "No update available or update in progress";
    return false;
  }

  status = DOWNLOADING;
  updateProgress = 0;
  spiffsProgress = 0;
  firmwareProgress = 0;
  phase = PHASE_NONE;
  Serial.println("[OTA] Starting firmware update...");

  if (spiffsUrl.length() > 0) {
    Serial.println("[OTA] Updating SPIFFS...");
    #include "ota_updater.h"
    #include "compat/compat.h"
    #include "esp_crt_bundle.h"
    #include "esp_http_client.h"
    #include "esp_https_ota.h"
    #include "esp_partition.h"
    #include "esp_ota_ops.h"
    #include <vector>

    #include "ota_updater.h"
    #include <esp_task_wdt.h>

    // =============================================================================
    // OTA UPDATER IMPLEMENTATION (ESP-IDF)
    // =============================================================================
namespace {
bool httpGetString(const String &url, std::string &out) {
  esp_http_client_config_t config = {
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return false;
  if (esp_http_client_open(client, 0) != ESP_OK) {
    esp_http_client_cleanup(client);
    return false;
  int content_length = esp_http_client_fetch_headers(client);
  if (content_length <= 0) {
    esp_http_client_close(client);
  out.clear();
  out.reserve(content_length);
  char buffer[512];
  while ((read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0) {
    out.append(buffer, read);
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return !out.empty();
}
} // namespace

void OTAUpdater::begin() {
  Serial.println("[OTA] Updater initialized");
  String currentVersion = getCurrentVersion();
  Serial.printf("[OTA] Current version: %s\n", currentVersion.c_str());
  Serial.printf("[OTA] Manifest URL: %s\n", MANIFEST_URL);
}

void OTAUpdater::checkForUpdates() {
  if (status == CHECKING || status == DOWNLOADING || status == INSTALLING) {
    return;
  }
  availableVersion = "";
  firmwareUrl = "";
  spiffsUrl = "";
  updateError = "";

  status = CHECKING;
  Serial.println("[OTA] Checking for updates...");
  String newVersion = "";
  String description = "";
  String firmwareName = "";
  String spiffsName = "";

  if (fetchManifest(newVersion, description, firmwareName, spiffsName)) {
    String currentVersion = getCurrentVersion();
    if (newVersion != currentVersion) {
  availableVersion = newVersion;
  firmwareUrl = firmwareName;
  spiffsUrl = spiffsName;
  status = UPDATE_AVAILABLE;
  Serial.printf("[OTA] Update available: %s -> %s\n", currentVersion.c_str(),
        newVersion.c_str());
      Serial.printf("[OTA] Description: %s\n", description.c_str());
    } else {
      status = NO_UPDATE;
      Serial.println("[OTA] Already on latest version");
    }
  } else {
  status = FAILED;
  updateError = "Failed to fetch manifest";
  Serial.println("[OTA] Failed to check for updates");
  }
}

bool OTAUpdater::startUpdate() {
  if (status != UPDATE_AVAILABLE) {
    updateError = "No update available or update in progress";
    return false;
  status = DOWNLOADING;
  updateProgress = 0;
  spiffsProgress = 0;
  firmwareProgress = 0;
  phase = PHASE_NONE;
  Serial.println("[OTA] Starting firmware update...");
  if (spiffsUrl.length() > 0) {
    Serial.println("[OTA] Updating SPIFFS...");
    if (!downloadAndUpdateSpiffs(spiffsUrl)) {
  status = FAILED;
  Serial.printf("[OTA] SPIFFS update failed: %s\n", updateError.c_str());
  return false;
    }
  }

  if (firmwareUrl.length() == 0) {
    firmwareUrl = FIRMWARE_URL;
  }

  if (downloadAndUpdate(firmwareUrl)) {
  status = SUCCESS;
  updateProgress = 100;
  phase = PHASE_NONE;
    Serial.println("[OTA] Update successful! Rebooting in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return true;  // Won't reach here
  }
  }

  status = FAILED;
  phase = PHASE_NONE;
  Serial.printf("[OTA] Update failed: %s\n", updateError.c_str());
  return false;
}

void OTAUpdater::cancelUpdate() {
  if (status == DOWNLOADING || status == INSTALLING) {
    status = IDLE;
    updateProgress = 0;
    Serial.println("[OTA] Update cancelled");
  }
}

void OTAUpdater::update() {}

bool OTAUpdater::fetchManifest(String &outVersion, String &outDescription,
                               String &outFirmware, String &outSpiffs) {
  String manifestUrl = String(MANIFEST_URL);
  manifestUrl += (manifestUrl.indexOf('?') >= 0 ? "&" : "?");
  manifestUrl += "t=" + String(millis());

  Serial.printf("[OTA] Fetching manifest from: %s\n", manifestUrl.c_str());

  std::string payload;
  if (!httpGetString(manifestUrl, payload)) {
    return false;
  }

  String payloadStr(payload);

  int versionStart = payloadStr.indexOf("\"version\":");
  if (versionStart == -1) {
    Serial.println("[OTA] Invalid manifest format");
    return false;
  }

  versionStart = payloadStr.indexOf("\"", versionStart + 10);
  int versionEnd = payloadStr.indexOf("\"", versionStart + 1);
  if (versionStart == -1 || versionEnd == -1) {
    return false;
  }
  outVersion = payloadStr.substring(versionStart + 1, versionEnd);

  int descStart = payloadStr.indexOf("\"description\":");
  if (descStart != -1) {
    descStart = payloadStr.indexOf("\"", descStart + 14);
    int descEnd = payloadStr.indexOf("\"", descStart + 1);
    if (descStart != -1 && descEnd != -1) {
      outDescription = payloadStr.substring(descStart + 1, descEnd);
    }
  }

  int fwStart = payloadStr.indexOf("\"firmware\":");
  if (fwStart != -1) {
    fwStart = payloadStr.indexOf("\"", fwStart + 11);
    int fwEnd = payloadStr.indexOf("\"", fwStart + 1);
    if (fwStart != -1 && fwEnd != -1) {
      outFirmware = payloadStr.substring(fwStart + 1, fwEnd);
    }
  }

  int spStart = payloadStr.indexOf("\"spiffs\":");
  if (spStart != -1) {
    spStart = payloadStr.indexOf("\"", spStart + 9);
    int spEnd = payloadStr.indexOf("\"", spStart + 1);
    if (spStart != -1 && spEnd != -1) {
      outSpiffs = payloadStr.substring(spStart + 1, spEnd);
    }
  }

  int lastSlash = String(MANIFEST_URL).lastIndexOf('/');
  if (lastSlash != -1) {
    String baseUrl = String(MANIFEST_URL).substring(0, lastSlash + 1);
    if (outFirmware.length() > 0) {
      outFirmware = baseUrl + outFirmware;
    }
    if (outSpiffs.length() > 0) {
      outSpiffs = baseUrl + outSpiffs;
    }
  }

  return true;
}

bool OTAUpdater::downloadAndUpdate(const String &firmwareUrl) {
  Serial.printf("[OTA] Downloading firmware from: %s\n", firmwareUrl.c_str());

  String downloadUrl = firmwareUrl;
  downloadUrl += (downloadUrl.indexOf('?') >= 0 ? "&" : "?");
  downloadUrl += "t=" + String(millis());

  esp_http_client_config_t config = {
      .url = downloadUrl.c_str(),
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 15000,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = &config,
  };

  esp_https_ota_handle_t ota_handle = nullptr;
  esp_err_t ret = esp_https_ota_begin(&ota_config, &ota_handle);
  if (ret != ESP_OK) {
    updateError = "OTA begin failed";
    return false;
  }

  phase = PHASE_FIRMWARE;
  status = INSTALLING;

  while (true) {
    ret = esp_https_ota_perform(ota_handle);
    if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      int image_len = esp_https_ota_get_image_len_read(ota_handle);
      int image_size = esp_https_ota_get_image_size(ota_handle);
      if (image_size > 0) {
        firmwareProgress = (image_len * 100) / image_size;
        updateProgress = firmwareProgress;
      }
      continue;
    }
    break;
  }

  if (ret != ESP_OK) {
    updateError = "OTA perform failed";
    esp_https_ota_abort(ota_handle);
    return false;
  }

  if (esp_https_ota_finish(ota_handle) != ESP_OK) {
    updateError = "OTA finish failed";
    return false;
  }

  firmwareProgress = 100;
  updateProgress = 100;
  return true;
}

bool OTAUpdater::downloadAndUpdateSpiffs(const String &spiffsUrl) {
  Serial.printf("[OTA] Downloading SPIFFS from: %s\n", spiffsUrl.c_str());

  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "littlefs");
  if (!partition) {
    updateError = "SPIFFS partition not found";
    return false;
  }

  String downloadUrl = spiffsUrl;
  downloadUrl += (downloadUrl.indexOf('?') >= 0 ? "&" : "?");
  downloadUrl += "t=" + String(millis());

  esp_http_client_config_t config = {
      .url = downloadUrl.c_str(),
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 15000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    updateError = "HTTP client init failed";
    return false;
  }

  if (esp_http_client_open(client, 0) != ESP_OK) {
    updateError = "HTTP open failed";
    esp_http_client_cleanup(client);
    return false;
  }

  int content_length = esp_http_client_fetch_headers(client);
  if (content_length <= 0) {
    updateError = "Invalid content length";
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  if (esp_partition_erase_range(partition, 0, partition->size) != ESP_OK) {
    updateError = "Partition erase failed";
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  phase = PHASE_SPIFFS;
  status = INSTALLING;

  const size_t bufferSize = 1024;
  std::vector<uint8_t> buffer(bufferSize);
  size_t written = 0;
  int read = 0;
  size_t offset = 0;

  while ((read = esp_http_client_read(client,
                                      reinterpret_cast<char *>(buffer.data()),
                                      buffer.size())) > 0) {
    if (esp_partition_write(partition, offset, buffer.data(), read) != ESP_OK) {
      updateError = "Partition write failed";
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    offset += read;
    written += read;
    spiffsProgress = (written * 100) / content_length;
    updateProgress = spiffsProgress;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  spiffsProgress = 100;
  return true;
}

    namespace {
    bool httpGetString(const String &url, std::string &out) {
      esp_http_client_config_t config = {
          .url = url.c_str(),
          .crt_bundle_attach = esp_crt_bundle_attach,
          .timeout_ms = 10000,
      };

      esp_http_client_handle_t client = esp_http_client_init(&config);
      if (!client) {
        return false;
      }

      if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
      }

      int content_length = esp_http_client_fetch_headers(client);
      if (content_length <= 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
      }

      out.clear();
      out.reserve(content_length);
      char buffer[512];
      int read = 0;
      while ((read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0) {
        out.append(buffer, read);
      }

      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return !out.empty();
    }
    } // namespace

    void OTAUpdater::begin() {
      Serial.println("[OTA] Updater initialized");
      String currentVersion = getCurrentVersion();
      Serial.printf("[OTA] Current version: %s\n", currentVersion.c_str());
      Serial.printf("[OTA] Manifest URL: %s\n", MANIFEST_URL);
    }

    void OTAUpdater::checkForUpdates() {
      if (status == CHECKING || status == DOWNLOADING || status == INSTALLING) {
        return;
      }

      availableVersion = "";
      firmwareUrl = "";
      spiffsUrl = "";
      updateError = "";

      status = CHECKING;
      Serial.println("[OTA] Checking for updates...");

      String newVersion = "";
      String description = "";
      String firmwareName = "";
      String spiffsName = "";

      if (fetchManifest(newVersion, description, firmwareName, spiffsName)) {
        String currentVersion = getCurrentVersion();
        if (newVersion != currentVersion) {
          availableVersion = newVersion;
          firmwareUrl = firmwareName;
          spiffsUrl = spiffsName;
          status = UPDATE_AVAILABLE;
          Serial.printf("[OTA] Update available: %s -> %s\n", currentVersion.c_str(),
                        newVersion.c_str());
          Serial.printf("[OTA] Description: %s\n", description.c_str());
        } else {
          status = NO_UPDATE;
          Serial.println("[OTA] Already on latest version");
        }
      } else {
        status = FAILED;
        updateError = "Failed to fetch manifest";
        Serial.println("[OTA] Failed to check for updates");
      }
    }

    bool OTAUpdater::startUpdate() {
      if (status != UPDATE_AVAILABLE) {
        updateError = "No update available or update in progress";
        return false;
      }

      status = DOWNLOADING;
      updateProgress = 0;
      spiffsProgress = 0;
      firmwareProgress = 0;
      phase = PHASE_NONE;
      Serial.println("[OTA] Starting firmware update...");

      if (spiffsUrl.length() > 0) {
        Serial.println("[OTA] Updating SPIFFS...");
        if (!downloadAndUpdateSpiffs(spiffsUrl)) {
          status = FAILED;
          Serial.printf("[OTA] SPIFFS update failed: %s\n", updateError.c_str());
          return false;
        }
      }

      if (firmwareUrl.length() == 0) {
        firmwareUrl = FIRMWARE_URL;
      }

      if (downloadAndUpdate(firmwareUrl)) {
        status = SUCCESS;
        updateProgress = 100;
        phase = PHASE_NONE;
        Serial.println("[OTA] Update successful! Rebooting in 2 seconds...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        return true;  // Won't reach here
      }

      status = FAILED;
      phase = PHASE_NONE;
      Serial.printf("[OTA] Update failed: %s\n", updateError.c_str());
      return false;
    }

    void OTAUpdater::cancelUpdate() {
      if (status == DOWNLOADING || status == INSTALLING) {
        status = IDLE;
        updateProgress = 0;
        Serial.println("[OTA] Update cancelled");
      }
    }

    void OTAUpdater::update() {}

    bool OTAUpdater::fetchManifest(String &outVersion, String &outDescription,
                                   String &outFirmware, String &outSpiffs) {
      String manifestUrl = String(MANIFEST_URL);
      manifestUrl += (manifestUrl.indexOf('?') >= 0 ? "&" : "?");
      manifestUrl += "t=" + String(millis());

      Serial.printf("[OTA] Fetching manifest from: %s\n", manifestUrl.c_str());

      std::string payload;
      if (!httpGetString(manifestUrl, payload)) {
        return false;
      }

      String payloadStr(payload);

      int versionStart = payloadStr.indexOf("\"version\":");
      if (versionStart == -1) {
        Serial.println("[OTA] Invalid manifest format");
        return false;
      }

      versionStart = payloadStr.indexOf("\"", versionStart + 10);
      int versionEnd = payloadStr.indexOf("\"", versionStart + 1);
      if (versionStart == -1 || versionEnd == -1) {
        return false;
      }
      outVersion = payloadStr.substring(versionStart + 1, versionEnd);

      int descStart = payloadStr.indexOf("\"description\":");
      if (descStart != -1) {
        descStart = payloadStr.indexOf("\"", descStart + 14);
        int descEnd = payloadStr.indexOf("\"", descStart + 1);
        if (descStart != -1 && descEnd != -1) {
          outDescription = payloadStr.substring(descStart + 1, descEnd);
        }
      }

      int fwStart = payloadStr.indexOf("\"firmware\":");
      if (fwStart != -1) {
        fwStart = payloadStr.indexOf("\"", fwStart + 11);
        int fwEnd = payloadStr.indexOf("\"", fwStart + 1);
        if (fwStart != -1 && fwEnd != -1) {
          outFirmware = payloadStr.substring(fwStart + 1, fwEnd);
        }
      }

      int spStart = payloadStr.indexOf("\"spiffs\":");
      if (spStart != -1) {
        spStart = payloadStr.indexOf("\"", spStart + 9);
        int spEnd = payloadStr.indexOf("\"", spStart + 1);
        if (spStart != -1 && spEnd != -1) {
          outSpiffs = payloadStr.substring(spStart + 1, spEnd);
        }
      }

      int lastSlash = String(MANIFEST_URL).lastIndexOf('/');
      if (lastSlash != -1) {
        String baseUrl = String(MANIFEST_URL).substring(0, lastSlash + 1);
        if (outFirmware.length() > 0) {
          outFirmware = baseUrl + outFirmware;
        }
        if (outSpiffs.length() > 0) {
          outSpiffs = baseUrl + outSpiffs;
        }
      }

      return true;
    }

    bool OTAUpdater::downloadAndUpdate(const String &firmwareUrl) {
      Serial.printf("[OTA] Downloading firmware from: %s\n", firmwareUrl.c_str());

      String downloadUrl = firmwareUrl;
      downloadUrl += (downloadUrl.indexOf('?') >= 0 ? "&" : "?");
      downloadUrl += "t=" + String(millis());

      esp_http_client_config_t config = {
          .url = downloadUrl.c_str(),
          .crt_bundle_attach = esp_crt_bundle_attach,
          .timeout_ms = 15000,
      };

      esp_https_ota_config_t ota_config = {
          .http_config = &config,
      };

      esp_https_ota_handle_t ota_handle = nullptr;
      esp_err_t ret = esp_https_ota_begin(&ota_config, &ota_handle);
      if (ret != ESP_OK) {
        updateError = "OTA begin failed";
        return false;
      }

      phase = PHASE_FIRMWARE;
      status = INSTALLING;

      while (true) {
        ret = esp_https_ota_perform(ota_handle);
        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
          int image_len = esp_https_ota_get_image_len_read(ota_handle);
          int image_size = esp_https_ota_get_image_size(ota_handle);
          if (image_size > 0) {
            firmwareProgress = (image_len * 100) / image_size;
            updateProgress = firmwareProgress;
          }
          continue;
        }
        break;
      }

      if (ret != ESP_OK) {
        updateError = "OTA perform failed";
        esp_https_ota_abort(ota_handle);
        return false;
      }

      if (esp_https_ota_finish(ota_handle) != ESP_OK) {
        updateError = "OTA finish failed";
        return false;
      }

      firmwareProgress = 100;
      updateProgress = 100;
      return true;
    }

    bool OTAUpdater::downloadAndUpdateSpiffs(const String &spiffsUrl) {
      Serial.printf("[OTA] Downloading SPIFFS from: %s\n", spiffsUrl.c_str());

      const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "littlefs");
      if (!partition) {
        updateError = "SPIFFS partition not found";
        return false;
      }

      String downloadUrl = spiffsUrl;
      downloadUrl += (downloadUrl.indexOf('?') >= 0 ? "&" : "?");
      downloadUrl += "t=" + String(millis());

      esp_http_client_config_t config = {
          .url = downloadUrl.c_str(),
          .crt_bundle_attach = esp_crt_bundle_attach,
          .timeout_ms = 15000,
      };

      esp_http_client_handle_t client = esp_http_client_init(&config);
      if (!client) {
        updateError = "HTTP client init failed";
        return false;
      }

      if (esp_http_client_open(client, 0) != ESP_OK) {
        updateError = "HTTP open failed";
        esp_http_client_cleanup(client);
        return false;
      }

      int content_length = esp_http_client_fetch_headers(client);
      if (content_length <= 0) {
        updateError = "Invalid content length";
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
      }

      if (esp_partition_erase_range(partition, 0, partition->size) != ESP_OK) {
        updateError = "Partition erase failed";
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
      }

      phase = PHASE_SPIFFS;
      status = INSTALLING;

      const size_t bufferSize = 1024;
      std::vector<uint8_t> buffer(bufferSize);
      size_t written = 0;
      int read = 0;
      size_t offset = 0;

      while ((read = esp_http_client_read(client, reinterpret_cast<char *>(buffer.data()), buffer.size())) > 0) {
        if (esp_partition_write(partition, offset, buffer.data(), read) != ESP_OK) {
          updateError = "Partition write failed";
          esp_http_client_close(client);
          esp_http_client_cleanup(client);
          return false;
        }
        offset += read;
        written += read;
        spiffsProgress = (written * 100) / content_length;
        updateProgress = spiffsProgress;
      }

      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      spiffsProgress = 100;
      return true;
    }
#endif
