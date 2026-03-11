#include "websocket_handler.h"
#include "control/pid_autotuner.h"
#include "control/temperature_controller.h"
#include "hardware/temperature.h"
#include "network/ota_updater.h"
#include "storage/persistent_storage.h"
#include "utils/time_sync.h"
#include <algorithm>
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

void otaUpdateTask(void *param) {
  (void)param;
  otaUpdater.startUpdate();
  otaTaskHandle = nullptr;
  vTaskDelete(nullptr);
}
} // namespace

WebSocketHandler::WebSocketHandler(StateCoordinator &coordinator)
    : stateCoord(coordinator) {}

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
    self->handleMessage(message);
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

void WebSocketHandler::handleMessage(const String &message) {
  if (message.startsWith("UpdateTimezone:")) {
    String newTz = message.substring(15);
    requestTimezoneChange(newTz);
    Serial.printf("[WebSocket] Updated Timezone: %s\n", newTz.c_str());
    updateClients();
    return;
  }

  Serial.printf("[WebSocket] Received: %s\n", message.c_str());

  if (message == "getHistory") {
    String historyData = stateCoord.getHistory().getHistoryJSON();
    updateClients(historyData);
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
    otaJson += ",\"otaCurrentVersion\":\"" + reportedVersion + "\"";
    String available = otaUpdater.getAvailableVersion();
    if (available.length() > 0) {
      otaJson += ",\"otaAvailableVersion\":\"" + available + "\"";
    }
    if (otaUpdater.getStatus() == OTAUpdater::DOWNLOADING ||
      otaUpdater.getStatus() == OTAUpdater::INSTALLING) {
      otaJson += ",\"otaProgress\":" + String(otaUpdater.getProgress());
      otaJson += ",\"otaSpiffsProgress\":" + String(otaUpdater.getSpiffsProgress());
      otaJson += ",\"otaFirmwareProgress\":" + String(otaUpdater.getFirmwareProgress());
      if (otaUpdater.getPhase() == OTAUpdater::PHASE_SPIFFS) {
        otaJson += ",\"otaPhase\":\"spiffs\"";
      } else if (otaUpdater.getPhase() == OTAUpdater::PHASE_FIRMWARE) {
        otaJson += ",\"otaPhase\":\"firmware\"";
      } else {
        otaJson += ",\"otaPhase\":\"none\"";
      }
    }
    if (otaUpdater.getStatus() == OTAUpdater::FAILED) {
      otaJson += ",\"otaError\":\"" + otaUpdater.getErrorMessage() + "\"";
    }
    otaJson += "}";
    updateClients(otaJson);
    return;
  }

  if (message == "checkOTAUpdates") {
    otaUpdater.checkForUpdates();
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
    ControllerState &ctrlState = stateCoord.getControllerMutable();
    DisplayState &display = stateCoord.getDisplayMutable();

    ctrlState.fanAuto = (mode == "auto");
    display.fanAuto = ctrlState.fanAuto;

    if (!ctrlState.fanAuto) {
      ctrlState.pidOutput = 0;
      ctrlState.fanPercent = 0;
      display.updateFanSpeed(0);
    }

    Serial.printf("[WebSocket] Fan mode set to: %s\n", mode.c_str());
    updateClients();
    return;
  }

  // Handle setpoint update (format: "2bVALUE")
  if (message.startsWith("2b")) {
    int newSetpoint = message.substring(2).toInt();
    ControllerState &ctrlState = stateCoord.getControllerMutable();

    // Reset PID state if setpoint changes significantly
    double setpointDelta = abs(newSetpoint - ctrlState.setpoint);
    if (setpointDelta > 10.0) {
      ctrlState.reset();
      Serial.printf(
          "[WebSocket] PID reset due to large setpoint change (%.1f°F)\n",
          setpointDelta);
    }

    ctrlState.setpoint = newSetpoint;

    DisplayState &display = stateCoord.getDisplayMutable();
    display.updateSetpoint(newSetpoint);

    Serial.printf("[WebSocket] Setpoint updated to: %d°F\n", newSetpoint);
    updateClients();
    return;
  }

  // Handle meat setpoint update (format: "8bVALUE")
  if (message.startsWith("8b")) {
    int newMeatSetpoint = message.substring(2).toInt();
    ControllerState &ctrlState = stateCoord.getControllerMutable();
    ctrlState.meatSetpoint = newMeatSetpoint;

    // Save to storage
    storage.saveMeatSetpoint(newMeatSetpoint);

    DisplayState &display = stateCoord.getDisplayMutable();
    display.meatSetpoint = String(newMeatSetpoint);

    Serial.printf("[WebSocket] Meat Setpoint updated to: %d°F\n",
                  newMeatSetpoint);
    updateClients();
    return;
  }

  // Handle keep warm setpoint update (format: "9bVALUE")
  if (message.startsWith("9b")) {
    int newKWSetpoint = message.substring(2).toInt();
    ControllerState &ctrlState = stateCoord.getControllerMutable();
    ctrlState.keepWarmSetpoint = newKWSetpoint;

    // Save to storage
    storage.saveKeepWarmSetpoint(newKWSetpoint);

    DisplayState &display = stateCoord.getDisplayMutable();
    display.keepWarmSetpoint = String(newKWSetpoint);

    Serial.printf("[WebSocket] Keep Warm Setpoint updated to: %d°F\n",
                  newKWSetpoint);
    updateClients();
    return;
  }

  // Handle alarm toggles (prepared for Phase 5)
  if (message.startsWith("KeepWarm")) {
    DisplayState &display = stateCoord.getDisplayMutable();
    display.keepWarmAlarm = message.substring(8);
    Serial.printf("[WebSocket] KeepWarm alarm: %s\n",
                  display.keepWarmAlarm.c_str());
    updateClients();
    return;
  }

  if (message.startsWith("PitTempLow")) {
    DisplayState &display = stateCoord.getDisplayMutable();
    display.pitTempLowAlarm = message.substring(10);
    Serial.printf("[WebSocket] PitTempLow alarm: %s\n",
                  display.pitTempLowAlarm.c_str());
    updateClients();
    return;
  }

  if (message.startsWith("DoneAlarm")) {
    DisplayState &display = stateCoord.getDisplayMutable();
    ControllerState &ctrl = stateCoord.getControllerMutable();
    display.doneAlarm = message.substring(9);
    ctrl.keepWarmEnabled = (display.doneAlarm == "true");
    Serial.printf("[WebSocket] DoneAlarm: %s (enabled: %d)\n",
                  display.doneAlarm.c_str(), ctrl.keepWarmEnabled);
    updateClients();
    return;
  }

  if (message.startsWith("LidDetection")) {
    String state = message.substring(12);
    DisplayState &display = stateCoord.getDisplayMutable();
    ControllerState &ctrl = stateCoord.getControllerMutable();

    display.lidDetectionAlarm = state;
    ctrl.lidDetectionEnabled = (state == "true");

    // If disabled, force-close any active detection
    if (!ctrl.lidDetectionEnabled) {
      ctrl.lidOpen = false;
      display.lidOpen = false;
    }

    Serial.printf("[WebSocket] LidDetection: %s\n", state.c_str());
    updateClients();
    return;
  }

  // Handle Calibration
  if (message.startsWith("CalibratePit:")) {
    int offset = message.substring(13).toInt();

    // Update hardware
    tempSensor.setPitOffset(offset);

    // Save to storage
    storage.saveTempOffsets(tempSensor.getPitOffset(),
                tempSensor.getMeatOffset());

    // Update display state
    DisplayState &display = stateCoord.getDisplayMutable();
    display.pitOffset = String(offset);

    Serial.printf("[WebSocket] Calibrate Pit Offset: %d\n", offset);
    updateClients();
    return;
  }

  if (message.startsWith("CalibrateMeat:")) {
    int offset = message.substring(14).toInt();

    // Update hardware
    tempSensor.setMeatOffset(offset);

    // Save to storage
    storage.saveTempOffsets(tempSensor.getPitOffset(),
                tempSensor.getMeatOffset());

    // Update display state
    DisplayState &display = stateCoord.getDisplayMutable();
    display.meatOffset = String(offset);

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
      double newKp = message.substring(firstColon + 1, secondColon).toDouble();
      double newKi = message.substring(secondColon + 1, thirdColon).toDouble();
      double newKd = message.substring(thirdColon + 1).toDouble();

      // Update hardware
      pidController.setTunings(newKp, newKi, newKd);

      // Save to storage
      storage.savePIDTunings(newKp, newKi, newKd);

      // Update display state
      DisplayState &display = stateCoord.getDisplayMutable();
      display.kp = String(newKp, 2);
      display.ki = String(newKi, 2);
      display.kd = String(newKd, 2);

      Serial.printf("[WebSocket] PID Updated - Kp: %.2f, Ki: %.2f, Kd: %.2f\n",
                    newKp, newKi, newKd);
      updateClients();
    }
    return;
  }

  // Handle Start Autotune
  if (message == "StartAutotune:true") {
    ControllerState &ctrl = stateCoord.getControllerMutable();
    if (!ctrl.autotuneActive) {
      autotuner.start(ctrl.setpoint, ctrl.pidOutput);
      ctrl.autotuneActive = true;
      updateClients();
    }
    return;
  }

  if (message == "StartAutotune:false") {
    ControllerState &ctrl = stateCoord.getControllerMutable();
    if (ctrl.autotuneActive) {
      autotuner.stop();
      ctrl.autotuneActive = false;
      updateClients();
    }
    return;
  }
}

void WebSocketHandler::onStateChanged() { updateClients(); }

void WebSocketHandler::updateClients() {
  if (otaUpdater.getStatus() == OTAUpdater::DOWNLOADING ||
      otaUpdater.getStatus() == OTAUpdater::INSTALLING) {
    return;
  }
  const DisplayState &display = stateCoord.getDisplay();
  String json = display.toJSON();
  updateClients(json);
}

void WebSocketHandler::updateClients(const String &customJson) {
  if (!server) {
    return;
  }
  
  // Skip if message is too large (prevent blocking)
  if (customJson.length() > 32768) {
    Serial.println("[WebSocket] Warning: Message too large, skipping");
    return;
  }
  
  httpd_ws_frame_t frame{};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(customJson.c_str()));
  frame.len = customJson.length();

  cleanupClients();
  for (int fd : clientFds) {
    esp_err_t ret = httpd_ws_send_frame_async(server, fd, &frame);
    if (ret != ESP_OK) {
      // Remove failed client immediately
      unregisterClient(fd);
    }
  }
}

void WebSocketHandler::cleanupClients() {
  if (!server) {
    return;
  }
  // Session validation helper is not available in this ESP-IDF version.
  // Keep existing client list; stale sockets will be cleaned up on send errors.
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
