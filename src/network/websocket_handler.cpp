#include "websocket_handler.h"
#include "control/pid_autotuner.h"
#include "config/control_config.h"
#include "control/temperature_controller.h"
#include "hardware/temperature.h"
#include "network/ota_updater.h"
#include "storage/persistent_storage.h"
#include "utils/time_sync.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <limits>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if defined(CONFIG_HTTPD_WS_SUPPORT) && CONFIG_HTTPD_WS_SUPPORT

// Extern global instances from main
extern PersistentStorage storage;
extern Temperature tempSensor;
extern TemperatureController pidController;
extern PIDAutotuner autotuner;
extern OTAUpdater otaUpdater;
extern TimeSync timeSync;
extern void requestTimezoneChange(const String &tz);

// =============================================================================
// WEBSOCKET HANDLER IMPLEMENTATION (ESP-IDF)
// =============================================================================

namespace {
TaskHandle_t otaTaskHandle = nullptr;
constexpr int kMinPitSetpoint = 150;
constexpr int kMaxPitSetpoint = 500;
constexpr int kMinMeatSetpoint = 80;
constexpr int kMaxMeatSetpoint = 250;
constexpr int kMinKeepWarmSetpoint = 120;
constexpr int kMaxKeepWarmSetpoint = 250;
constexpr int kMinProbeOffset = -50;
constexpr int kMaxProbeOffset = 50;
constexpr size_t kMaxTimezoneLength = 63;
constexpr size_t kHistoryChunkPoints = 120;
constexpr double kMinPidGain = 0.0;
constexpr double kMaxPidGain = 1000.0;

String escapeJsonString(const String &input) {
  std::string escaped;
  escaped.reserve(input.length() + 8);
  for (char ch : input.str()) {
    switch (ch) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\b':
      escaped += "\\b";
      break;
    case '\f':
      escaped += "\\f";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(ch) < 0x20) {
        char buffer[7];
        snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(ch));
        escaped += buffer;
      } else {
        escaped.push_back(ch);
      }
      break;
    }
  }
  return String(escaped);
}

bool parseStrictInt(const String &value, int &out) {
  std::string raw = value.str();
  if (raw.empty()) {
    return false;
  }
  char *end = nullptr;
  errno = 0;
  long parsed = std::strtol(raw.c_str(), &end, 10);
  if (errno != 0 || end == raw.c_str() || *end != '\0' ||
      parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    return false;
  }
  out = static_cast<int>(parsed);
  return true;
}

bool parseStrictDouble(const String &value, double &out) {
  std::string raw = value.str();
  if (raw.empty()) {
    return false;
  }
  char *end = nullptr;
  errno = 0;
  double parsed = std::strtod(raw.c_str(), &end);
  if (errno != 0 || end == raw.c_str() || *end != '\0' || !std::isfinite(parsed)) {
    return false;
  }
  out = parsed;
  return true;
}

int clampInt(int value, int minValue, int maxValue) {
  return std::max(minValue, std::min(value, maxValue));
}

double clampDouble(double value, double minValue, double maxValue) {
  return std::max(minValue, std::min(value, maxValue));
}

bool isBooleanString(const String &value) {
  return value == "true" || value == "false";
}

bool isValidTimezoneValue(const String &value) {
  if (value.isEmpty() || value.length() > kMaxTimezoneLength) {
    return false;
  }
  for (char ch : value.str()) {
    if (static_cast<unsigned char>(ch) < 0x20) {
      return false;
    }
  }
  return true;
}

void otaUpdateTask(void *param) {
  (void)param;
  otaUpdater.startUpdate();
  otaTaskHandle = nullptr;
  vTaskDelete(nullptr);
}
} // namespace

WebSocketHandler::WebSocketHandler(StateCoordinator &coordinator)
    : stateCoord(coordinator) {}

WebSocketHandler::~WebSocketHandler() { stateCoord.removeObserver(this); }

void WebSocketHandler::init(httpd_handle_t httpdServer) {
  server = httpdServer;

  httpd_uri_t ws_uri = {
      .uri = "/ws",
      .method = HTTP_GET,
      .handler = &WebSocketHandler::handleWs,
      .user_ctx = this,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = nullptr,
  };
  httpd_register_uri_handler(server, &ws_uri);

  stateCoord.addObserver(this);
  Serial.println("[OK] WebSocket handler initialized");
}

esp_err_t WebSocketHandler::handleWs(httpd_req_t *req) {
  auto *self = static_cast<WebSocketHandler *>(req->user_ctx);

  if (req->method == HTTP_GET) {
    self->registerClient(httpd_req_to_sockfd(req));
    self->updateClients();
    return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt{};
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  if (ws_pkt.len) {
    std::vector<uint8_t> buf(ws_pkt.len + 1);
    ws_pkt.payload = buf.data();
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
      return ret;
    }
    buf[ws_pkt.len] = 0;
    String message(reinterpret_cast<char *>(buf.data()));
    self->handleMessage(message, httpd_req_to_sockfd(req));
  }

  return ESP_OK;
}

void WebSocketHandler::registerClient(int fd) {
  if (std::find(clientFds.begin(), clientFds.end(), fd) == clientFds.end()) {
    clientFds.push_back(fd);
  }
}

void WebSocketHandler::unregisterClient(int fd) {
  clientFds.erase(std::remove(clientFds.begin(), clientFds.end(), fd), clientFds.end());
}

void WebSocketHandler::sendToClient(int fd, const String &json) {
  if (!server) {
    return;
  }

  if (json.length() > 32768) {
    Serial.println("[WebSocket] Warning: Message too large, skipping");
    return;
  }

  httpd_ws_frame_t frame{};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(json.c_str()));
  frame.len = json.length();

  if (httpd_ws_send_frame_async(server, fd, &frame) != ESP_OK) {
    unregisterClient(fd);
  }
}

void WebSocketHandler::sendHistoryChunk(int fd, size_t start) {
  sendToClient(fd, stateCoord.getHistoryChunkJSON(start, kHistoryChunkPoints));
}

void WebSocketHandler::handleMessage(const String &message, int clientFd) {
  if (message.startsWith("UpdateTimezone:")) {
    String newTz = message.substring(15);
    if (!isValidTimezoneValue(newTz)) {
      Serial.println("[WebSocket] Rejected invalid timezone update");
      return;
    }
    requestTimezoneChange(newTz);
    Serial.printf("[WebSocket] Updated Timezone: %s\n", newTz.c_str());
    updateClients();
    return;
  }

  Serial.printf("[WebSocket] Received: %s\n", message.c_str());

  if (message == "getHistory") {
    sendHistoryChunk(clientFd, 0);
    return;
  }

  if (message.startsWith("getHistoryChunk:")) {
    int start = 0;
    if (!parseStrictInt(message.substring(16), start) || start < 0) {
      Serial.println("[WebSocket] Rejected invalid history chunk request");
      return;
    }
    sendHistoryChunk(clientFd, static_cast<size_t>(start));
    return;
  }

  if (message == "getValues") {
    updateClients();
    return;
  }

  if (message == "getOTAInfo") {
    String otaStatus = "idle";
    switch (otaUpdater.getStatus()) {
    case OTAUpdater::CHECKING:
      otaStatus = "checking";
      break;
    case OTAUpdater::UPDATE_AVAILABLE:
      otaStatus = "update_available";
      break;
    case OTAUpdater::DOWNLOADING:
      otaStatus = "downloading";
      break;
    case OTAUpdater::INSTALLING:
      otaStatus = "installing";
      break;
    case OTAUpdater::SUCCESS:
      otaStatus = "success";
      break;
    case OTAUpdater::FAILED:
      otaStatus = "failed";
      break;
    case OTAUpdater::NO_UPDATE:
      otaStatus = "no_update";
      break;
    case OTAUpdater::IDLE:
    default:
      otaStatus = "idle";
      break;
    }

    String otaJson = "{";
    otaJson += "\"otaStatus\":\"" + otaStatus + "\"";
    String storedVersion = storage.loadFirmwareVersion();
    String reportedVersion = storedVersion.length() > 0
                   ? storedVersion
                   : otaUpdater.getCurrentVersion();
    if (otaUpdater.getStatus() == OTAUpdater::SUCCESS &&
        otaUpdater.getAvailableVersion().length() > 0) {
      reportedVersion = otaUpdater.getAvailableVersion();
      storage.saveFirmwareVersion(otaUpdater.getAvailableVersion());
      otaUpdater.setCurrentVersion(otaUpdater.getAvailableVersion());
    }
    otaJson += ",\"otaCurrentVersion\":\"" + escapeJsonString(reportedVersion) + "\"";
    String available = otaUpdater.getAvailableVersion();
    if (available.length() > 0) {
      otaJson += ",\"otaAvailableVersion\":\"" + escapeJsonString(available) + "\"";
    }
    String description = otaUpdater.getAvailableDescription();
    if (description.length() > 0) {
      otaJson += ",\"otaDescription\":\"" + escapeJsonString(description) + "\"";
    }
    if (otaUpdater.getStatus() == OTAUpdater::DOWNLOADING ||
      otaUpdater.getStatus() == OTAUpdater::INSTALLING) {
      otaJson += ",\"otaProgress\":" + String(otaUpdater.getProgress());
      otaJson += ",\"otaLittlefsProgress\":" + String(otaUpdater.getLittlefsProgress());
      otaJson += ",\"otaFirmwareProgress\":" + String(otaUpdater.getFirmwareProgress());
      if (otaUpdater.getPhase() == OTAUpdater::PHASE_LITTLEFS) {
        otaJson += ",\"otaPhase\":\"littlefs\"";
      } else if (otaUpdater.getPhase() == OTAUpdater::PHASE_FIRMWARE) {
        otaJson += ",\"otaPhase\":\"firmware\"";
      } else {
        otaJson += ",\"otaPhase\":\"none\"";
      }
    }
    if (otaUpdater.getStatus() == OTAUpdater::FAILED) {
      otaJson += ",\"otaError\":\"" + escapeJsonString(otaUpdater.getErrorMessage()) + "\"";
    }
    otaJson += "}";
    updateClients(otaJson);
    return;
  }

  if (message == "checkOTAUpdates") {
    otaUpdater.checkForUpdates();
    return;
  }

  if (message == "cancelOTAUpdate") {
    otaUpdater.cancelUpdate();
    updateClients();
    return;
  }

  if (message == "startOTAUpdate") {
    if (otaTaskHandle == nullptr) {
      xTaskCreatePinnedToCore(otaUpdateTask, "ota_update", 8192, nullptr, 1,
                              &otaTaskHandle, 1);
    }
    return;
  }

  if (message.startsWith("FanMode:")) {
    String mode = message.substring(8);
    if (mode == "off") {
      mode = "manual";
    }
    if (mode != "auto" && mode != "manual") {
      Serial.printf("[WebSocket] Rejected invalid fan mode: %s\n", mode.c_str());
      return;
    }
    stateCoord.withState([&](SensorData &, ControllerState &ctrlState,
                             DisplayState &display, HistoryManager &) {
      ctrlState.fanAuto = (mode == "auto");
      display.fanAuto = ctrlState.fanAuto;

      if (!ctrlState.fanAuto) {
        ctrlState.pidOutput = 0;
        ctrlState.fanPercent = 0;
        display.updateFanSpeed(0);
      }
    });

    Serial.printf("[WebSocket] Fan mode set to: %s\n", mode.c_str());
    updateClients();
    return;
  }

  // Handle setpoint update (format: "2bVALUE")
  if (message.startsWith("2b")) {
    int newSetpoint = 0;
    if (!parseStrictInt(message.substring(2), newSetpoint)) {
      Serial.println("[WebSocket] Rejected invalid pit setpoint");
      return;
    }
    newSetpoint = clampInt(newSetpoint, kMinPitSetpoint, kMaxPitSetpoint);
    double setpointDelta = 0.0;
    stateCoord.withState([&](SensorData &, ControllerState &ctrlState,
                             DisplayState &display, HistoryManager &) {
      setpointDelta = abs(newSetpoint - ctrlState.setpoint);
      if (setpointDelta > 10.0) {
        ctrlState.reset();
      }

      ctrlState.setpoint = newSetpoint;
      display.updateSetpoint(newSetpoint);
    });

    if (setpointDelta > 10.0) {
      Serial.printf(
          "[WebSocket] PID reset due to large setpoint change (%.1f°F)\n",
          setpointDelta);
    }

    Serial.printf("[WebSocket] Setpoint updated to: %d°F\n", newSetpoint);
    updateClients();
    return;
  }

  // Handle meat setpoint update (format: "8bVALUE")
  if (message.startsWith("8b")) {
    int newMeatSetpoint = 0;
    if (!parseStrictInt(message.substring(2), newMeatSetpoint)) {
      Serial.println("[WebSocket] Rejected invalid meat setpoint");
      return;
    }
    newMeatSetpoint = clampInt(newMeatSetpoint, kMinMeatSetpoint, kMaxMeatSetpoint);
    stateCoord.withState([&](SensorData &, ControllerState &ctrlState,
                             DisplayState &display, HistoryManager &) {
      ctrlState.meatSetpoint = newMeatSetpoint;
      display.meatSetpoint = String(newMeatSetpoint);
    });

    // Save to storage
    storage.saveMeatSetpoint(newMeatSetpoint);

    Serial.printf("[WebSocket] Meat Setpoint updated to: %d°F\n",
                  newMeatSetpoint);
    updateClients();
    return;
  }

  // Handle keep warm setpoint update (format: "9bVALUE")
  if (message.startsWith("9b")) {
    int newKWSetpoint = 0;
    if (!parseStrictInt(message.substring(2), newKWSetpoint)) {
      Serial.println("[WebSocket] Rejected invalid keep warm setpoint");
      return;
    }
    newKWSetpoint = clampInt(newKWSetpoint, kMinKeepWarmSetpoint, kMaxKeepWarmSetpoint);
    stateCoord.withState([&](SensorData &, ControllerState &ctrlState,
                             DisplayState &display, HistoryManager &) {
      ctrlState.keepWarmSetpoint = newKWSetpoint;
      display.keepWarmSetpoint = String(newKWSetpoint);
    });

    // Save to storage
    storage.saveKeepWarmSetpoint(newKWSetpoint);

    Serial.printf("[WebSocket] Keep Warm Setpoint updated to: %d°F\n",
                  newKWSetpoint);
    updateClients();
    return;
  }

  // Handle alarm toggles (prepared for Phase 5)
  if (message.startsWith("KeepWarm")) {
    if (!isBooleanString(message.substring(8))) {
      Serial.println("[WebSocket] Rejected invalid KeepWarm state");
      return;
    }
    String value = message.substring(8);
    stateCoord.withDisplay([&](DisplayState &display) {
      display.keepWarmAlarm = value;
    });
    Serial.printf("[WebSocket] KeepWarm alarm: %s\n",
                  value.c_str());
    updateClients();
    return;
  }

  if (message.startsWith("DoneAlarm")) {
    if (!isBooleanString(message.substring(9))) {
      Serial.println("[WebSocket] Rejected invalid DoneAlarm state");
      return;
    }
    String value = message.substring(9);
    bool keepWarmEnabled = (value == "true");
    stateCoord.withState([&](SensorData &, ControllerState &ctrl,
                             DisplayState &display, HistoryManager &) {
      display.doneAlarm = value;
      ctrl.keepWarmEnabled = keepWarmEnabled;
    });
    Serial.printf("[WebSocket] DoneAlarm: %s (enabled: %d)\n",
                  value.c_str(), keepWarmEnabled);
    updateClients();
    return;
  }

  // Handle Calibration
  if (message.startsWith("CalibratePit:")) {
    int offset = 0;
    if (!parseStrictInt(message.substring(13), offset)) {
      Serial.println("[WebSocket] Rejected invalid pit calibration offset");
      return;
    }
    offset = clampInt(offset, kMinProbeOffset, kMaxProbeOffset);

    // Update hardware
    tempSensor.setPitOffset(offset);

    // Save to storage
    storage.saveTempOffsets(tempSensor.getPitOffset(),
                tempSensor.getMeatOffset());

    // Update display state
    stateCoord.withDisplay([&](DisplayState &display) {
      display.pitOffset = String(offset);
    });

    Serial.printf("[WebSocket] Calibrate Pit Offset: %d\n", offset);
    updateClients();
    return;
  }

  if (message.startsWith("CalibrateMeat:")) {
    int offset = 0;
    if (!parseStrictInt(message.substring(14), offset)) {
      Serial.println("[WebSocket] Rejected invalid meat calibration offset");
      return;
    }
    offset = clampInt(offset, kMinProbeOffset, kMaxProbeOffset);

    // Update hardware
    tempSensor.setMeatOffset(offset);

    // Save to storage
    storage.saveTempOffsets(tempSensor.getPitOffset(),
                tempSensor.getMeatOffset());

    // Update display state
    stateCoord.withDisplay([&](DisplayState &display) {
      display.meatOffset = String(offset);
    });

    Serial.printf("[WebSocket] Calibrate Meat Offset: %d\n", offset);
    updateClients();
    return;
  }

  // Handle PID tuning update (format: "UpdatePID:Kp:Ki:Kd")
  if (message.startsWith("UpdatePID:")) {
    // Message format: UpdatePID:10.0:0.05:2.0
    int firstColon = message.indexOf(':');
    int secondColon = message.indexOf(':', firstColon + 1);
    int thirdColon = message.indexOf(':', secondColon + 1);

    if (firstColon != -1 && secondColon != -1 && thirdColon != -1) {
      double newKp = 0.0;
      double newKi = 0.0;
      double newKd = 0.0;
      if (!parseStrictDouble(message.substring(firstColon + 1, secondColon), newKp) ||
          !parseStrictDouble(message.substring(secondColon + 1, thirdColon), newKi) ||
          !parseStrictDouble(message.substring(thirdColon + 1), newKd)) {
        Serial.println("[WebSocket] Rejected invalid PID update payload");
        return;
      }
      newKp = clampDouble(newKp, kMinPidGain, kMaxPidGain);
      newKi = clampDouble(newKi, kMinPidGain, kMaxPidGain);
      newKd = clampDouble(newKd, kMinPidGain, kMaxPidGain);

      // Update hardware
      pidController.setTunings(newKp, newKi, newKd);

      // Save to storage
      storage.savePIDTunings(newKp, newKi, newKd);

      // Update display state
      stateCoord.withDisplay([&](DisplayState &display) {
        display.kp = String(newKp, 2);
        display.ki = String(newKi, 2);
        display.kd = String(newKd, 2);
      });

      Serial.printf("[WebSocket] PID Updated - Kp: %.2f, Ki: %.2f, Kd: %.2f\n",
                    newKp, newKi, newKd);
      updateClients();
    } else {
      Serial.println("[WebSocket] Rejected malformed PID update payload");
    }
    return;
  }

  // Handle Start Autotune
  if (message == "StartAutotune:true") {
    bool started = false;
    stateCoord.withController([&](ControllerState &ctrl) {
      if (!ctrl.autotuneActive) {
        autotuner.start(ctrl.setpoint, ctrl.pidOutput);
        ctrl.autotuneActive = true;
        started = true;
      }
    });
    if (started) {
      updateClients();
    }
    return;
  }

  if (message == "StartAutotune:false") {
    bool stopped = false;
    stateCoord.withController([&](ControllerState &ctrl) {
      if (ctrl.autotuneActive) {
        autotuner.stop();
        ctrl.autotuneActive = false;
        stopped = true;
      }
    });
    if (stopped) {
      updateClients();
    }
    return;
  }

  Serial.printf("[WebSocket] Unhandled message: %s\n", message.c_str());
}

void WebSocketHandler::onStateChanged() { updateClients(); }

void WebSocketHandler::updateClients() {
  if (otaUpdater.getStatus() == OTAUpdater::DOWNLOADING ||
      otaUpdater.getStatus() == OTAUpdater::INSTALLING) {
    return;
  }
  DisplayState display = stateCoord.getDisplay();
  String json = display.toJSON();
  updateClients(json);
}

void WebSocketHandler::updateClients(const String &customJson) {
  if (!server) {
    return;
  }

  if (customJson.length() > 32768) {
    Serial.println("[WebSocket] Warning: Message too large, skipping");
    return;
  }

  httpd_ws_frame_t frame{};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(customJson.c_str()));
  frame.len = customJson.length();

  cleanupClients();
  std::vector<int> failedClients;
  for (int fd : clientFds) {
    esp_err_t ret = httpd_ws_send_frame_async(server, fd, &frame);
    if (ret != ESP_OK) {
      failedClients.push_back(fd);
    }
  }

  for (int fd : failedClients) {
    unregisterClient(fd);
  }
}

void WebSocketHandler::cleanupClients() {
  if (!server) {
    return;
  }

  std::vector<int> duplicateFree;
  duplicateFree.reserve(clientFds.size());
  for (int fd : clientFds) {
    if (std::find(duplicateFree.begin(), duplicateFree.end(), fd) ==
        duplicateFree.end()) {
      duplicateFree.push_back(fd);
    }
  }
  clientFds.swap(duplicateFree);
}

#else

WebSocketHandler::WebSocketHandler(StateCoordinator &coordinator)
    : stateCoord(coordinator) {}

void WebSocketHandler::init(httpd_handle_t httpdServer) {
  (void)httpdServer;
  Serial.println("[WARN] WebSocket support not enabled");
}

void WebSocketHandler::onStateChanged() {}

void WebSocketHandler::updateClients() {}

void WebSocketHandler::updateClients(const String &customJson) {
  (void)customJson;
}

void WebSocketHandler::cleanupClients() {}

#endif
