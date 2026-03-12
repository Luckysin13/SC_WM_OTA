#include "ota_updater.h"
#include "compat/compat.h"
#include "esp_crt_bundle.h"
#include "esp_efuse.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include <cJSON.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <strings.h>
#include <vector>

extern bool saveHistorySnapshotForRestart();

// =============================================================================
// OTA UPDATER IMPLEMENTATION (ESP-IDF)
// =============================================================================

namespace {
static const char kOtaPinnedCertPem[] = "";
constexpr size_t kSha256Bytes = 32;
constexpr size_t kSha256HexLength = kSha256Bytes * 2;

struct ManifestArtifact {
  String path;
  size_t size = 0;
  String sha256;
};

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

  int contentLength = esp_http_client_fetch_headers(client);
  int statusCode = esp_http_client_get_status_code(client);
  Serial.printf("[OTA] HTTP status: %d, content-length: %d\n", statusCode,
                contentLength);
  if (statusCode < 200 || statusCode >= 300) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  out.clear();
  out.reserve(contentLength > 0 ? static_cast<size_t>(contentLength) : 1024U);

  char buffer[512];
  int bytesRead = 0;
  while ((bytesRead = esp_http_client_read(client, buffer, sizeof(buffer))) >
         0) {
    out.append(buffer, bytesRead);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return !out.empty();
}

bool parseRequiredString(cJSON *object, const char *key, String &out) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsString(item) || item->valuestring == nullptr ||
      item->valuestring[0] == '\0') {
    return false;
  }
  out = item->valuestring;
  return true;
}

bool parseOptionalUint32(cJSON *object, const char *key, uint32_t &out) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item) {
    return true;
  }
  if (!cJSON_IsNumber(item) || item->valuedouble < 0) {
    return false;
  }
  out = static_cast<uint32_t>(item->valuedouble);
  return true;
}

bool looksLikeSha256(const String &value) {
  if (value.length() != kSha256HexLength) {
    return false;
  }

  for (char ch : value.str()) {
    if (!std::isxdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }

  return true;
}

String toHexString(const uint8_t *bytes, size_t len) {
  static const char *digits = "0123456789abcdef";
  std::string value;
  value.reserve(len * 2);

  for (size_t index = 0; index < len; ++index) {
    value.push_back(digits[(bytes[index] >> 4) & 0x0F]);
    value.push_back(digits[bytes[index] & 0x0F]);
  }

  return String(value);
}

bool partitionMatchesSha256(const esp_partition_t *partition,
                            const String &expectedSha256) {
  if (!partition || expectedSha256.length() == 0) {
    return true;
  }

  uint8_t digest[kSha256Bytes] = {0};
  if (esp_partition_get_sha256(partition, digest) != ESP_OK) {
    return false;
  }

  return toHexString(digest, sizeof(digest)) == expectedSha256;
}

bool extractArtifact(cJSON *root, cJSON *artifacts, const char *rootKey,
                     const char *artifactKey, ManifestArtifact &out) {
  cJSON *rootItem = cJSON_GetObjectItemCaseSensitive(root, rootKey);
  if (cJSON_IsString(rootItem) && rootItem->valuestring != nullptr) {
    out.path = rootItem->valuestring;
  }

  cJSON *artifact = artifacts
                        ? cJSON_GetObjectItemCaseSensitive(artifacts, artifactKey)
                        : nullptr;
  if (!cJSON_IsObject(artifact)) {
    return out.path.length() > 0;
  }

  String manifestPath;
  if (!parseRequiredString(artifact, "path", manifestPath)) {
    return false;
  }

  cJSON *sizeItem = cJSON_GetObjectItemCaseSensitive(artifact, "size");
  cJSON *shaItem = cJSON_GetObjectItemCaseSensitive(artifact, "sha256");
  if (!cJSON_IsNumber(sizeItem) || sizeItem->valuedouble <= 0 ||
      !cJSON_IsString(shaItem) || shaItem->valuestring == nullptr) {
    return false;
  }

  out.path = manifestPath;
  out.size = static_cast<size_t>(sizeItem->valuedouble);
  out.sha256 = shaItem->valuestring;
  return looksLikeSha256(out.sha256);
}

bool hasArtifactMetadata(const ManifestArtifact &artifact) {
  return artifact.size > 0 && looksLikeSha256(artifact.sha256);
}

int compareMainVersion(const std::string &left, const std::string &right) {
  size_t leftPos = 0;
  size_t rightPos = 0;

  while (leftPos < left.size() || rightPos < right.size()) {
    size_t leftNext = left.find('.', leftPos);
    size_t rightNext = right.find('.', rightPos);
    std::string leftToken =
        left.substr(leftPos, leftNext == std::string::npos ? std::string::npos
                                                            : leftNext - leftPos);
    std::string rightToken =
        right.substr(rightPos, rightNext == std::string::npos ? std::string::npos
                                                               : rightNext - rightPos);

    long leftValue =
        leftToken.empty() ? 0 : std::strtol(leftToken.c_str(), nullptr, 10);
    long rightValue =
        rightToken.empty() ? 0 : std::strtol(rightToken.c_str(), nullptr, 10);
    if (leftValue != rightValue) {
      return leftValue < rightValue ? -1 : 1;
    }

    leftPos = leftNext == std::string::npos ? left.size() : leftNext + 1;
    rightPos = rightNext == std::string::npos ? right.size() : rightNext + 1;
  }

  return 0;
}

int compareVersionStrings(const String &leftVersion, const String &rightVersion) {
  std::string left = leftVersion.str();
  std::string right = rightVersion.str();

  size_t leftDash = left.find('-');
  size_t rightDash = right.find('-');
  int mainCompare = compareMainVersion(left.substr(0, leftDash),
                                       right.substr(0, rightDash));
  if (mainCompare != 0) {
    return mainCompare;
  }

  bool leftHasPre = leftDash != std::string::npos;
  bool rightHasPre = rightDash != std::string::npos;
  if (leftHasPre != rightHasPre) {
    return leftHasPre ? -1 : 1;
  }
  if (!leftHasPre) {
    return 0;
  }

  std::string leftPre = left.substr(leftDash + 1);
  std::string rightPre = right.substr(rightDash + 1);
  if (leftPre == rightPre) {
    return 0;
  }
  return leftPre < rightPre ? -1 : 1;
}

String makeArtifactUrl(const String &path) {
  if (path.length() == 0) {
    return String();
  }
  if (path.startsWith("http://") || path.startsWith("https://")) {
    return path;
  }

  int lastSlash = String(OTAUpdater::MANIFEST_URL).lastIndexOf('/');
  if (lastSlash == -1) {
    return path;
  }

  String baseUrl = String(OTAUpdater::MANIFEST_URL).substring(0, lastSlash + 1);
  return baseUrl + path;
}
} // namespace

void OTAUpdater::begin() {
  Serial.println("[OTA] Updater initialized");

  esp_app_desc_t runningAppDesc = {};
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running &&
      esp_ota_get_partition_description(running, &runningAppDesc) == ESP_OK) {
    String runningVersion = String(runningAppDesc.version);
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
  availableDescription = "";
  availableSecureVersion = 0;
  expectedFirmwareSize = 0;
  expectedSpiffsSize = 0;
  expectedFirmwareSha256 = "";
  expectedSpiffsSha256 = "";
  updateError = "";

  status = CHECKING;
  Serial.println("[OTA] Checking for updates...");

  String newVersion = "";
  String description = "";
  String firmwareName = "";
  String spiffsName = "";
  uint32_t secureVersion = 0;

  if (!fetchManifest(newVersion, description, firmwareName, spiffsName,
                     secureVersion)) {
    status = FAILED;
    if (updateError.length() == 0) {
      updateError = "Failed to fetch manifest";
    }
    Serial.println("[OTA] Failed to check for updates");
    return;
  }

  String currentVersion = getCurrentVersion();
  int versionCompare = compareVersionStrings(newVersion, currentVersion);
  if (versionCompare > 0) {
    availableVersion = newVersion;
    firmwareUrl = firmwareName;
    spiffsUrl = spiffsName;
    availableDescription = description;
    availableSecureVersion = secureVersion;
    status = UPDATE_AVAILABLE;
    Serial.printf("[OTA] Update available: %s -> %s\n", currentVersion.c_str(),
                  newVersion.c_str());
    Serial.printf("[OTA] Description: %s\n", description.c_str());
    return;
  }

  status = NO_UPDATE;
  if (versionCompare == 0) {
    Serial.println("[OTA] Already on latest version");
  } else {
    Serial.printf("[OTA] Ignoring older release %s (current %s)\n",
                  newVersion.c_str(), currentVersion.c_str());
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
    esp_app_desc_t runningAppDesc = {};
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_partition_description(running, &runningAppDesc) == ESP_OK) {
      if (availableSecureVersion <= runningAppDesc.secure_version) {
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
    saveHistorySnapshotForRestart();
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return true;
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
    updateError = "Manifest download failed";
    return false;
  }

  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
      cJSON_ParseWithLength(payload.data(), payload.size()), &cJSON_Delete);
  if (!root) {
    updateError = "Manifest JSON parse failed";
    Serial.println("[OTA] Invalid manifest JSON");
    return false;
  }

  if (!parseRequiredString(root.get(), "version", outVersion)) {
    updateError = "Manifest missing version";
    return false;
  }

  cJSON *descriptionItem =
      cJSON_GetObjectItemCaseSensitive(root.get(), "description");
  if (cJSON_IsString(descriptionItem) && descriptionItem->valuestring != nullptr) {
    outDescription = descriptionItem->valuestring;
  }

  if (!parseOptionalUint32(root.get(), "secure_version", outSecureVersion)) {
    updateError = "Manifest secure_version invalid";
    return false;
  }

  cJSON *artifacts = cJSON_GetObjectItemCaseSensitive(root.get(), "artifacts");
  if (artifacts != nullptr && !cJSON_IsObject(artifacts)) {
    updateError = "Manifest artifacts invalid";
    return false;
  }

  ManifestArtifact firmwareArtifact;
  ManifestArtifact littlefsArtifact;
  if (!extractArtifact(root.get(), artifacts, "firmware", "firmware",
                       firmwareArtifact) ||
      firmwareArtifact.path.length() == 0) {
    updateError = "Manifest missing firmware artifact";
    return false;
  }

  if ((!extractArtifact(root.get(), artifacts, "littlefs", "littlefs",
                        littlefsArtifact) &&
       !extractArtifact(root.get(), artifacts, "spiffs", "spiffs",
                        littlefsArtifact)) ||
      littlefsArtifact.path.length() == 0) {
    updateError = "Manifest missing filesystem artifact";
    return false;
  }

  expectedFirmwareSize = hasArtifactMetadata(firmwareArtifact)
                             ? firmwareArtifact.size
                             : 0;
  expectedFirmwareSha256 = hasArtifactMetadata(firmwareArtifact)
                               ? firmwareArtifact.sha256
                               : String();
  expectedSpiffsSize = hasArtifactMetadata(littlefsArtifact)
                           ? littlefsArtifact.size
                           : 0;
  expectedSpiffsSha256 = hasArtifactMetadata(littlefsArtifact)
                             ? littlefsArtifact.sha256
                             : String();

  outFirmware = makeArtifactUrl(firmwareArtifact.path);
  outSpiffs = makeArtifactUrl(littlefsArtifact.path);
  return outFirmware.length() > 0;
}

bool OTAUpdater::downloadAndUpdate(const String &firmwareUrl) {
  Serial.printf("[OTA] Downloading firmware from: %s\n", firmwareUrl.c_str());

  String downloadUrl = firmwareUrl;
  downloadUrl += (downloadUrl.indexOf('?') >= 0 ? "&" : "?");
  downloadUrl += "t=" + String(static_cast<unsigned long>(millis()));

  esp_http_client_config_t config = {};
  configureHttpClient(config, downloadUrl.c_str(), 15000);

  esp_https_ota_config_t otaConfig = {};
  otaConfig.http_config = &config;
  otaConfig.partial_http_download = true;
  otaConfig.max_http_request_size = 8192;

  esp_https_ota_handle_t otaHandle = nullptr;
  esp_err_t ret = esp_https_ota_begin(&otaConfig, &otaHandle);
  if (ret != ESP_OK) {
    Serial.printf("[OTA] Begin failed: %s\n", esp_err_to_name(ret));
    updateError = "OTA begin failed: " + String(esp_err_to_name(ret));
    return false;
  }

  esp_app_desc_t newAppDesc = {};
  if (esp_https_ota_get_img_desc(otaHandle, &newAppDesc) == ESP_OK) {
    Serial.printf("[OTA] New firmware version: %s\n", newAppDesc.version);
    if (availableVersion.length() > 0 &&
        String(newAppDesc.version) != availableVersion) {
      Serial.printf("[OTA] Warning: image version %s differs from manifest %s\n",
                    newAppDesc.version, availableVersion.c_str());
    }
  }

  phase = PHASE_FIRMWARE;
  status = INSTALLING;

  size_t fallbackTotal = 0;
  const esp_partition_t *updatePartition =
      esp_ota_get_next_update_partition(nullptr);
  if (updatePartition) {
    fallbackTotal = updatePartition->size;
    Serial.printf("[OTA] Target partition: %s, size: %d\n",
                  updatePartition->label, updatePartition->size);
  }

  int imageSize = esp_https_ota_get_image_size(otaHandle);
  Serial.printf("[OTA] Image size from header: %d bytes\n", imageSize);
  if (expectedFirmwareSize > 0 && imageSize > 0 &&
      static_cast<size_t>(imageSize) != expectedFirmwareSize) {
    updateError = "Firmware size mismatch";
    esp_https_ota_abort(otaHandle);
    return false;
  }

  while (true) {
    ret = esp_https_ota_perform(otaHandle);
    if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      int imageLen = esp_https_ota_get_image_len_read(otaHandle);
      if (imageSize > 0) {
        firmwareProgress = (imageLen * 100) / imageSize;
        updateProgress = firmwareProgress;
      } else if (fallbackTotal > 0) {
        int fallbackProgress =
            (imageLen * 100) / static_cast<int>(fallbackTotal);
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

  int finalLen = esp_https_ota_get_image_len_read(otaHandle);
  Serial.printf("[OTA] Download complete. Bytes read: %d, expected: %d\n",
                finalLen, imageSize);

  if (ret != ESP_OK) {
    Serial.printf("[OTA] Perform failed: %s\n", esp_err_to_name(ret));
    updateError = "OTA perform failed: " + String(esp_err_to_name(ret));
    esp_https_ota_abort(otaHandle);
    return false;
  }

  if (!esp_https_ota_is_complete_data_received(otaHandle)) {
    Serial.println("[OTA] ERROR: Complete data was not received!");
    updateError = "OTA incomplete - data not fully received";
    esp_https_ota_abort(otaHandle);
    return false;
  }

  Serial.println("[OTA] Data complete, finishing OTA...");
  esp_err_t finishErr = esp_https_ota_finish(otaHandle);
  if (finishErr != ESP_OK) {
    Serial.printf("[OTA] Finish failed: %s\n", esp_err_to_name(finishErr));
    if (finishErr == ESP_ERR_OTA_VALIDATE_FAILED) {
      updateError = "OTA validation failed - image may be unsigned or corrupted";
    } else {
      updateError = "OTA finish failed: " + String(esp_err_to_name(finishErr));
    }
    return false;
  }

  if (!partitionMatchesSha256(updatePartition, expectedFirmwareSha256)) {
    updateError = "Firmware checksum mismatch";
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
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                         ESP_PARTITION_SUBTYPE_DATA_LITTLEFS,
                                         nullptr);
  }
  if (!partition) {
    Serial.println("[OTA] LittleFS partition not found, listing all partitions:");
    esp_partition_iterator_t iterator =
        esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY,
                           nullptr);
    while (iterator != nullptr) {
      const esp_partition_t *entry = esp_partition_get(iterator);
      Serial.printf(
          "[OTA] Found partition: %s, type: %d, subtype: %d, offset: 0x%08x, size: 0x%08x\n",
          entry->label, entry->type, entry->subtype, entry->address,
          entry->size);
      iterator = esp_partition_next(iterator);
    }
    esp_partition_iterator_release(iterator);
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

  int contentLength = esp_http_client_fetch_headers(client);
  int statusCode = esp_http_client_get_status_code(client);
  char *contentEncoding = nullptr;
  if (esp_http_client_get_header(client, "Content-Encoding", &contentEncoding) !=
      ESP_OK) {
    contentEncoding = nullptr;
  }
  Serial.printf("[OTA] LittleFS HTTP status: %d, content-length: %d, encoding: %s\n",
                statusCode, contentLength,
                contentEncoding ? contentEncoding : "identity");

  if (statusCode < 200 || statusCode >= 300) {
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
  if (expectedSpiffsSize > 0 && contentLength > 0 &&
      static_cast<size_t>(contentLength) != expectedSpiffsSize) {
    updateError = "LittleFS size mismatch";
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

  std::vector<uint8_t> buffer(1024);
  size_t written = 0;
  size_t offset = 0;
  int bytesRead = 0;

  while ((bytesRead = esp_http_client_read(
              client, reinterpret_cast<char *>(buffer.data()), buffer.size())) >
         0) {
    if (esp_partition_write(partition, offset, buffer.data(), bytesRead) !=
        ESP_OK) {
      updateError = "Partition write failed";
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }

    offset += static_cast<size_t>(bytesRead);
    written += static_cast<size_t>(bytesRead);
    if (contentLength > 0) {
      spiffsProgress = static_cast<int>((written * 100U) /
                                        static_cast<size_t>(contentLength));
    } else if (partition->size > 0) {
      spiffsProgress = static_cast<int>((written * 100U) /
                                        static_cast<size_t>(partition->size));
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
  if (contentLength > 0 && written != static_cast<size_t>(contentLength)) {
    updateError = "LittleFS download incomplete";
    return false;
  }
  if (expectedSpiffsSize > 0 && written != expectedSpiffsSize) {
    updateError = "LittleFS size mismatch";
    return false;
  }
  if (!partitionMatchesSha256(partition, expectedSpiffsSha256)) {
    updateError = "LittleFS checksum mismatch";
    return false;
  }

  spiffsProgress = 100;
  updateProgress = 100;
  return true;
}
