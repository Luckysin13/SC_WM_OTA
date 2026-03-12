// =============================================================================
// Smoker Controller - Phase 4: AP Configuration Mode
// =============================================================================
// Adds Access Point mode for initial WiFi configuration
// Automatic fallback to AP mode if STA connection fails
// Complete modular refactoring from original 699-line monolithic file
// =============================================================================

#include "compat/compat.h"
#include <cstring>
#include <memory>
#include "driver/gpio.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_system.h"
#include "freertos/queue.h"

// Configuration
#include "config/control_config.h"
#include "config/debug_config.h"
#include "config/hardware_config.h"
#include "config/network_config.h"
#include "config/paths_config.h"
#include "config/ssl_cert.h"

// Hardware modules
#include "hardware/fan_actuator.h"
#include "hardware/pins.h"
#include "hardware/temperature.h"

// Control modules
#include "control/controller_state.h"
#include "control/display_state.h"
#include "control/lid_open_detector.h"
#include "control/pid_autotuner.h"
#include "control/sensor_data.h"
#include "control/state_coordinator.h"
#include "control/temperature_controller.h"

// Storage modules
#include "storage/persistent_storage.h"

// Network modules
#include "network/ap_mode_handler.h"
#include "network/network_manager.h"
#include "network/sta_mode_handler.h"
#include "network/websocket_handler.h"
#include "network/ota_updater.h"

// Utility modules
#include "utils/time_sync.h"

// =============================================================================
// GLOBAL OBJECTS
// =============================================================================
// Hardware
Temperature tempSensor;
FanActuator fanActuator;
TemperatureController pidController;
LidOpenDetector lidDetector;
PIDAutotuner autotuner;
StateCoordinator stateCoord;

// Storage
PersistentStorage storage;

// Network
NetworkManager networkMgr;
httpd_handle_t server = nullptr;

// Time
TimeSync timeSync;

// WebSocket handler (pointer to manage lifecycle)
std::unique_ptr<WebSocketHandler> wsHandler;

OTAUpdater otaUpdater;

// AP mode handler (pointer to manage lifecycle)
std::unique_ptr<APModeHandler> apHandler;

// STA mode handler (pointer to manage lifecycle)
std::unique_ptr<STAModeHandler> staHandler;

namespace {
QueueHandle_t s_tzQueue = nullptr;
volatile bool s_restartRequested = false;
unsigned long s_restartAtMs = 0;
volatile bool s_wifiReadyForUser = false;
constexpr unsigned long kHistorySnapshotIntervalMs = 3UL * 60UL * 1000UL;

bool shouldRestoreHistoryForReset(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_SW:
  case ESP_RST_PANIC:
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT:
    return true;
  default:
    return false;
  }
}

bool saveHistorySnapshot(bool force) {
  if (!storage.isMounted()) {
    return false;
  }

  if (!force && !stateCoord.historyNeedsSnapshot()) {
    return false;
  }

  std::string snapshot = stateCoord.serializeHistorySnapshot();
  if (snapshot.empty()) {
    storage.clearHistorySnapshot();
    stateCoord.markHistorySnapshotSaved();
    return true;
  }

  bool ok = storage.saveHistorySnapshot(snapshot);
  if (ok) {
    stateCoord.markHistorySnapshotSaved();
  }
  return ok;
}

void restoreHistorySnapshotForBoot(esp_reset_reason_t resetReason) {
  if (!storage.isMounted()) {
    return;
  }

  if (!shouldRestoreHistoryForReset(resetReason)) {
    storage.clearHistorySnapshot();
    return;
  }

  std::string snapshot = storage.loadHistorySnapshot();
  if (snapshot.empty()) {
    return;
  }

  if (stateCoord.restoreHistorySnapshot(snapshot)) {
    Serial.printf("[HISTORY] Restored %u points after restart\n",
                  static_cast<unsigned>(stateCoord.getHistoryCount()));
    return;
  }

  Serial.println("[HISTORY] Snapshot restore failed; clearing snapshot");
  storage.clearHistorySnapshot();
}

void shutdownRuntimeServices() {
  wsHandler.reset();
  staHandler.reset();
  apHandler.reset();

  if (server != nullptr) {
    httpd_stop(server);
    server = nullptr;
    Serial.println("[NET] HTTP server stopped");
  }
}

void handleRollbackIfNeeded() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    return;
  }
  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }

  esp_ota_mark_app_valid_cancel_rollback();
  Serial.println("[OTA] App marked valid; rollback canceled");
}
}

bool saveHistorySnapshotForRestart() { return saveHistorySnapshot(true); }

void prepareForRestart() {
  s_wifiReadyForUser = false;
  saveHistorySnapshotForRestart();
  shutdownRuntimeServices();
}

void requestSystemRestart(unsigned long delayMs) {
  s_restartAtMs = millis() + delayMs;
  s_restartRequested = true;
  Serial.printf("[SYS] Restart requested in %lu ms\n", delayMs);
}

// =============================================================================
// WiFi LED TASK
// =============================================================================
void wifiLedTask(void *param) {
  (void)param;

  enum class WifiLedMode {
    Off,
    Blink,
    Solid,
  };
  
  // Removed task start print to reduce serial spam
  
  bool ledState = false;
  unsigned long lastTime = millis();
  unsigned long pendingSince = millis();
  bool lastStateWasSolid = false;
  bool debugPrinted = false;
  int loopCount = 0;
  unsigned long lastWifiCheck = 0;
  bool isStaConnected = false;
  WifiLedMode appliedMode = WifiLedMode::Off;
  WifiLedMode pendingMode = WifiLedMode::Off;
  
  while (1) {
    wifi_mode_t wifiMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&wifiMode);
    bool wifiReadyForUser = s_wifiReadyForUser;
    WifiLedMode desiredMode = WifiLedMode::Off;
    
    // Check WiFi connection status only once per second
    unsigned long now = millis();
    if (now - lastWifiCheck >= 1000) {
      lastWifiCheck = now;
      // Check if STA is actually connected (has valid connection and IP)
      isStaConnected = false;
      if (wifiMode == WIFI_MODE_STA || wifiMode == WIFI_MODE_APSTA) {
        // Try to get AP info - only works if STA is connected
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
          isStaConnected = true;
        }
      }
    }
    
    loopCount++;
    
    // Debug every 50 loops
    if (loopCount % 50 == 1) {
      // Removed debug print to reduce serial spam
    }
    
    if (!wifiReadyForUser) {
      desiredMode = WifiLedMode::Off;
    } else if (isStaConnected && wifiMode == WIFI_MODE_STA) {
      desiredMode = WifiLedMode::Solid;
    } else if (wifiMode == WIFI_MODE_AP ||
               (wifiMode == WIFI_MODE_APSTA && !isStaConnected)) {
      desiredMode = WifiLedMode::Blink;
    } else if (isStaConnected && wifiMode == WIFI_MODE_APSTA) {
      desiredMode = WifiLedMode::Solid;
    }

    if (desiredMode != pendingMode) {
      pendingMode = desiredMode;
      pendingSince = now;
    }

    if (appliedMode != pendingMode && now - pendingSince >= 3000) {
      appliedMode = pendingMode;
      lastTime = now;
      ledState = false;
    }

    if (appliedMode == WifiLedMode::Solid) {
      gpio_set_level(static_cast<gpio_num_t>(PIN_WIFI_LED), 1);
      if (!lastStateWasSolid) {
        // Removed print
        lastStateWasSolid = true;
        debugPrinted = false;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (appliedMode == WifiLedMode::Blink) {
      if (!debugPrinted) {
        // Removed print
        debugPrinted = true;
        lastTime = now;
      }
      
      if (now - lastTime >= 500) {
        lastTime = now;
        ledState = !ledState;
        gpio_set_level(static_cast<gpio_num_t>(PIN_WIFI_LED), ledState ? 1 : 0);
        // Removed toggle print to reduce serial spam
      }
      lastStateWasSolid = false;
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // Fallback: LED OFF
    gpio_set_level(static_cast<gpio_num_t>(PIN_WIFI_LED), 0);
    if (lastStateWasSolid || debugPrinted) {
      // Removed print to reduce serial spam
      lastStateWasSolid = false;
      debugPrinted = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void requestTimezoneChange(const String &tz) {
  if (!s_tzQueue) {
    return;
  }
  char buffer[64] = {0};
  size_t len = tz.length();
  if (len >= sizeof(buffer)) {
    len = sizeof(buffer) - 1;
  }
  memcpy(buffer, tz.c_str(), len);
  xQueueOverwrite(s_tzQueue, buffer);
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  s_wifiReadyForUser = false;
  esp_reset_reason_t resetReason = esp_reset_reason();

  // Initialize serial communication
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n===========================================");
  Serial.println("Smoker Controller - Phase 4");
  Serial.println("Complete Modular Architecture");
  Serial.println("===========================================\n");
  Serial.printf("[SYS] Reset reason: %d\n", static_cast<int>(resetReason));

  handleRollbackIfNeeded();

  // Initialize hardware pins
  HardwarePins::init();
  Serial.println("[OK] Hardware pins initialized");

  if (!s_tzQueue) {
    s_tzQueue = xQueueCreate(1, 64);
  }

  // Check for factory reset
  if (HardwarePins::isResetPressed()) {
    Serial.println("[WARNING] Factory reset requested!");
    if (storage.begin()) {
      storage.eraseCredentials();
      Serial.println("[OK] Credentials erased");
    }
    Serial.println("Remove jumper and restart to continue");
    while (1) {
      delay(1000);
    }
  }

  // Initialize LittleFS
  bool storageReady = storage.begin();
  if (!storageReady) {
    Serial.println("[ERROR] LittleFS initialization failed!");
    Serial.println("Continuing without persisted WiFi settings...");
  }

  if (storageReady) {
    String storedFirmwareVersion = storage.loadFirmwareVersion();
    if (storedFirmwareVersion.length() > 0) {
      otaUpdater.setCurrentVersion(storedFirmwareVersion);
    }
  }

  // Load WiFi credentials (or empty to force AP)
  WiFiCredentials creds;
  if (storageReady) {
    creds = storage.loadCredentials();
    Serial.printf("[DEBUG] Loaded credentials - SSID length: %d\n", creds.ssid.length());
  } else {
    Serial.println("[DEBUG] Storage not ready, starting in AP mode");
  }

  // Initialize network (tries STA, falls back to AP)
  Serial.println("[DEBUG] Calling networkMgr.begin()...");
  NetworkMode mode = networkMgr.begin(creds);
  Serial.printf("[DEBUG] networkMgr.begin() returned mode: %d\n", (int)mode);

  // Start web server - use plain HTTP for both modes (faster, no TLS overhead)
  bool httpServerStarted = false;
  Serial.printf("[DEBUG] About to start web server, mode=%d\n", (int)mode);
  
  if (mode == NetworkMode::STA || mode == NetworkMode::AP) {
    // Use plain HTTP for both STA and AP modes
    Serial.println("[DEBUG] Creating HTTP config...");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 16384;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 20;
    config.lru_purge_enable = true;
    
    Serial.println("[DEBUG] Calling httpd_start()...");
    esp_err_t httpRet = httpd_start(&server, &config);
    Serial.printf("[DEBUG] httpd_start returned: %d (%s)\n", httpRet, esp_err_to_name(httpRet));
    if (httpRet == ESP_OK) {
      httpServerStarted = true;
      Serial.println("[OK] HTTP server started successfully");
    } else {
      Serial.printf("[ERROR] Failed to start HTTP server: %s\n", esp_err_to_name(httpRet));
    }
  }
  
  // Initialize WebSocket handler if server started
  if (httpServerStarted) {
    wsHandler = std::make_unique<WebSocketHandler>(stateCoord);
    wsHandler->init(server);
    
    Serial.println("[OK] Web server started");
    Serial.printf("  Access at: http://%s/\n",
                  networkMgr.getLocalIP().toString().c_str());
  }

  // Setup handlers AFTER starting the web server (only if server started)
  if (httpServerStarted && mode == NetworkMode::STA) {
    // STA Mode: Connected to existing network
    Serial.println("\n[OK] Network initialized in STA mode");

    // Setup web server routes for normal operation
    staHandler = std::make_unique<STAModeHandler>(server, storage, networkMgr);
    staHandler->setupRoutes();

    // Initialize NTP time sync with stored timezone
    if (storageReady) {
      String savedTz = storage.loadTimezone();
      timeSync.begin(savedTz.c_str());
    }

    s_wifiReadyForUser = true;

  } else if (httpServerStarted && mode == NetworkMode::AP) {
    // AP Mode: Configuration portal
    Serial.println("\n[OK] Network initialized in AP mode");
    if (!storageReady) {
      Serial.println("[WARN] LittleFS unavailable; web UI may not load");
    }

    // Setup web server routes for configuration
    apHandler = std::make_unique<APModeHandler>(server, storage, networkMgr);
    apHandler->setupRoutes();

    s_wifiReadyForUser = true;

  } else if (mode != NetworkMode::NONE) {
    // HTTP server failed to start but network is up
    Serial.println("\n[ERROR] HTTP server failed but network is available");
    s_wifiReadyForUser = false;
  } else {
    Serial.println("\n[ERROR] Network initialization failed");
    s_wifiReadyForUser = false;
  }
  
  // Start WiFi LED task after WiFi is initialized
  Serial.println("[LED] Creating WiFi LED task...");
  BaseType_t ret = xTaskCreate(wifiLedTask, "wifi_led", 2048, nullptr, 1, nullptr);
  if (ret == pdPASS) {
    Serial.println("[LED] Task created successfully!");
  } else {
    Serial.printf("[LED] ERROR: Task creation failed! (ret=%d)\n", ret);
  }

  otaUpdater.begin();

  // Initialize temperature sensor
  if (!tempSensor.begin()) {
    Serial.println("[ERROR] Temperature sensor initialization failed!");
    while (1) {
      delay(1000);
    }
  }

  // Load and apply calibration offsets
  int pitOffset = 0;
  int meatOffset = 0;
  storage.loadTempOffsets(pitOffset, meatOffset);
  tempSensor.setPitOffset(pitOffset);
  tempSensor.setMeatOffset(meatOffset);

  Serial.println("[OK] Temperature sensor initialized");
  Serial.printf("[OK] Applied offsets - Pit: %d, Meat: %d\n", pitOffset,
                meatOffset);

  // Initialize fan actuator
  if (!fanActuator.begin()) {
    Serial.println("[ERROR] Fan actuator initialization failed!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("[OK] Fan actuator initialized");

  // Initialize PID controller
  double kp, ki, kd;
  storage.loadPIDTunings(kp, ki, kd);
  pidController.setTunings(kp, ki, kd);
  pidController.begin();
  Serial.println("[OK] PID controller initialized");

  // Load timezone for display
  String tz = storage.loadTimezone();
  // Timezone already applied during timeSync.begin() if in STA mode

  // Initialize state coordinator
  stateCoord.begin();
  restoreHistorySnapshotForBoot(resetReason);

  // Initialize display state
  double defaultSetpoint = DEFAULT_SETPOINT;
  stateCoord.withState([&](SensorData &, ControllerState &ctrlState,
                           DisplayState &display, HistoryManager &) {
    ctrlState.setpoint = DEFAULT_SETPOINT;
    ctrlState.meatSetpoint = storage.loadMeatSetpoint();
    ctrlState.keepWarmSetpoint = storage.loadKeepWarmSetpoint();

    display.updateSetpoint(ctrlState.setpoint);
    display.pitOffset = String(pitOffset);
    display.meatOffset = String(meatOffset);
    display.kp = String(kp, 2);
    display.ki = String(ki, 2);
    display.kd = String(kd, 2);
    display.timezone = tz;
    display.meatSetpoint = String((int)ctrlState.meatSetpoint);
    display.keepWarmSetpoint = String((int)ctrlState.keepWarmSetpoint);
    defaultSetpoint = ctrlState.setpoint;
  });

  Serial.printf("[OK] Default setpoint: %.0f°F\n", defaultSetpoint);

  Serial.println("\n===========================================");
  Serial.println("Performing Sensor Warmup...");
  for (int i = 0; i < 50; i++) {
    tempSensor.readPitTemp();
    tempSensor.readMeatTemp();
    if (i % 10 == 0)
      Serial.print(".");
  }
  Serial.println("\nWarmup complete!");
  Serial.println("Starting control loop...");
  Serial.println("===========================================\n");

  delay(500);
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
  static unsigned long lastHistorySnapshotMs = 0;

  if (s_restartRequested && static_cast<long>(millis() - s_restartAtMs) >= 0) {
    s_restartRequested = false;
    prepareForRestart();
    delay(100);
    ESP::restart();
  }

  // Cleanup disconnected WebSocket clients (STA mode only)
  if (wsHandler != nullptr) {
    wsHandler->cleanupClients();
  }

  // OTA update state machine
  if (otaUpdater.getStatus() != OTAUpdater::IDLE) {
    // Send OTA status to all clients
    String otaJson = "{";
    switch (otaUpdater.getStatus()) {
      case OTAUpdater::CHECKING:
        otaJson += "\"otaStatus\":\"checking\"";
        break;
      case OTAUpdater::UPDATE_AVAILABLE:
        otaJson += "\"otaStatus\":\"update_available\"";
        break;
      case OTAUpdater::NO_UPDATE:
        otaJson += "\"otaStatus\":\"no_update\"";
        break;
      case OTAUpdater::DOWNLOADING:
        otaJson += "\"otaStatus\":\"downloading\",\"otaProgress\":" + String(otaUpdater.getProgress());
        otaJson += ",\"otaSpiffsProgress\":" + String(otaUpdater.getSpiffsProgress());
        otaJson += ",\"otaFirmwareProgress\":" + String(otaUpdater.getFirmwareProgress());
        if (otaUpdater.getPhase() == OTAUpdater::PHASE_SPIFFS) {
          otaJson += ",\"otaPhase\":\"spiffs\"";
        } else if (otaUpdater.getPhase() == OTAUpdater::PHASE_FIRMWARE) {
          otaJson += ",\"otaPhase\":\"firmware\"";
        } else {
          otaJson += ",\"otaPhase\":\"none\"";
        }
        break;
      case OTAUpdater::INSTALLING:
        otaJson += "\"otaStatus\":\"installing\",\"otaProgress\":" + String(otaUpdater.getProgress());
        otaJson += ",\"otaSpiffsProgress\":" + String(otaUpdater.getSpiffsProgress());
        otaJson += ",\"otaFirmwareProgress\":" + String(otaUpdater.getFirmwareProgress());
        if (otaUpdater.getPhase() == OTAUpdater::PHASE_SPIFFS) {
          otaJson += ",\"otaPhase\":\"spiffs\"";
        } else if (otaUpdater.getPhase() == OTAUpdater::PHASE_FIRMWARE) {
          otaJson += ",\"otaPhase\":\"firmware\"";
        } else {
          otaJson += ",\"otaPhase\":\"none\"";
        }
        break;
      case OTAUpdater::SUCCESS:
        otaJson += "\"otaStatus\":\"success\"";
        break;
      case OTAUpdater::FAILED:
        otaJson += "\"otaStatus\":\"failed\",\"otaError\":\"" + otaUpdater.getErrorMessage() + "\"";
        break;
      default:
        otaJson += "\"otaStatus\":\"idle\"";
        break;
    }
    String available = otaUpdater.getAvailableVersion();
    String storedVersion = storage.loadFirmwareVersion();
    String reportedVersion = storedVersion.length() > 0
                                 ? storedVersion
                                 : otaUpdater.getCurrentVersion();
    if (otaUpdater.getStatus() == OTAUpdater::SUCCESS &&
        available.length() > 0) {
      reportedVersion = available;
      storage.saveFirmwareVersion(available);
      otaUpdater.setCurrentVersion(available);
    }
    otaJson += ",\"otaCurrentVersion\":\"" + reportedVersion + "\"";
    if (available.length() > 0) {
      otaJson += ",\"otaAvailableVersion\":\"" + available + "\"";
    }
    otaJson += "}";
    if (wsHandler) wsHandler->updateClients(otaJson);
    if (otaUpdater.getStatus() == OTAUpdater::SUCCESS && otaUpdater.shouldReboot()) {
      prepareForRestart();
      delay(1000);
      ESP::restart();
    }
  }

  // Read temperature sensors
  SensorData sensorData;
  sensorData.pitTemp = tempSensor.readPitTemp();
  sensorData.meatTemp = tempSensor.readMeatTemp();
  stateCoord.updateSensors(sensorData);

  double pidOutput = 0;
  stateCoord.withState([&](SensorData &, ControllerState &ctrlState,
                           DisplayState &display, HistoryManager &) {
    ctrlState.lidOpen = lidDetector.update(sensorData.pitTemp, ctrlState.setpoint);

    if (ctrlState.lidOpen) {
      ctrlState.lastInput = sensorData.pitTemp;
      pidOutput = 0;

      if (ctrlState.autotuneActive) {
        ctrlState.autotuneActive = false;
        autotuner.stop();
        Serial.println("[Autotune] Cancelled: Lid opened.");
      }
    } else if (ctrlState.autotuneActive) {
      pidOutput = autotuner.update(sensorData.pitTemp);
      ctrlState.pidOutput = pidOutput;

      if (autotuner.getState() == PIDAutotuner::State::COMPLETE) {
        double newKp, newKi, newKd;
        autotuner.getResults(newKp, newKi, newKd);

        pidController.setTunings(newKp, newKi, newKd);
        storage.savePIDTunings(newKp, newKi, newKd);

        display.kp = String(newKp, 2);
        display.ki = String(newKi, 2);
        display.kd = String(newKd, 2);

        ctrlState.autotuneActive = false;
        Serial.println("[Autotune] Applied and finished.");
      } else if (autotuner.getState() == PIDAutotuner::State::FAILED) {
        ctrlState.autotuneActive = false;
        Serial.println("[Autotune] Failed and stopped.");
      }
    } else {
      pidOutput = pidController.compute(sensorData.pitTemp, ctrlState);
    }

    if (ctrlState.keepWarmEnabled &&
        Temperature::isValidTemp(sensorData.meatTemp) &&
        sensorData.meatTemp >= (ctrlState.meatSetpoint + 0.5) &&
        ctrlState.setpoint != ctrlState.keepWarmSetpoint) {
      Serial.printf("[KeepWarm] Meat reached target (%.1f). Dropping pit "
                    "setpoint to %.1f\n",
                    sensorData.meatTemp, ctrlState.keepWarmSetpoint);
      ctrlState.setpoint = ctrlState.keepWarmSetpoint;
      ctrlState.reset();
    }

    if (!ctrlState.fanAuto) {
      fanActuator.setDutyCycle(0);
      ctrlState.pidOutput = 0;
      ctrlState.fanPercent = 0;
    } else if (pidOutput >= FAN_MIN_DUTY &&
               Temperature::isValidTemp(sensorData.pitTemp)) {
      fanActuator.setDutyCycle((int)pidOutput);
      ctrlState.fanPercent = fanActuator.getSpeedPercent();
    } else {
      fanActuator.setDutyCycle(0);
      ctrlState.fanPercent = 0;
    }
  });

  // Update display state for WebSocket transmission (Both AP and STA modes)
  if (networkMgr.getMode() != NetworkMode::NONE) {
    stateCoord.withState([&](SensorData &, ControllerState &ctrlState,
                             DisplayState &display, HistoryManager &) {
      display.isAP = (networkMgr.getMode() == NetworkMode::AP);
      display.wifiConnected = networkMgr.isConnected();
      display.wifiIp = networkMgr.getLocalIP().toString();
      if (display.isAP) {
        display.wifiSsid = String(AP_SSID);
        display.wifiRssi = String(0);
      } else if (display.wifiConnected) {
        display.wifiSsid = networkMgr.getSSID();
        display.wifiRssi = String(networkMgr.getRSSI());
      } else {
        display.wifiSsid = "";
        display.wifiRssi = String(0);
      }

      display.updateMeatTemp(sensorData.meatTemp,
                             Temperature::isValidTemp(sensorData.meatTemp));
      display.updatePitTemp(sensorData.pitTemp,
                            Temperature::isValidTemp(sensorData.pitTemp));
      display.updateSetpoint(ctrlState.setpoint);
      display.meatSetpoint = String((int)ctrlState.meatSetpoint);
      display.keepWarmSetpoint = String((int)ctrlState.keepWarmSetpoint);
      display.updateFanSpeed(ctrlState.fanPercent);
      display.lidOpen = ctrlState.lidOpen;
      display.autotuneActive = ctrlState.autotuneActive;
      display.autotuneState = (int)autotuner.getState();
      display.fanAuto = ctrlState.fanAuto;

      if (s_tzQueue) {
        char tzBuf[64] = {0};
        if (xQueueReceive(s_tzQueue, &tzBuf, 0) == pdTRUE) {
          String newTz = String(tzBuf);
          if (newTz.length() > 0) {
            timeSync.setTimezone(newTz.c_str());
            storage.saveTimezone(newTz);
            display.timezone = newTz;
          }
        }
      }
    });

    stateCoord.updateDisplay();
  }

  ControllerState ctrlState = stateCoord.getController();

  // Print status to serial
  Serial.println("-------------------------------------------");

  // Network status
  NetworkMode mode = networkMgr.getMode();
  if (mode == NetworkMode::STA && networkMgr.isConnected()) {
    Serial.printf("WiFi: Connected (STA) - %s, RSSI: %d dBm\n",
                  networkMgr.getSSID().c_str(), networkMgr.getRSSI());
    Serial.printf("IP Address: %s\n",
                  networkMgr.getLocalIP().toString().c_str());

    // Update and display time if available
    if (timeSync.isInitialized() && timeSync.updateTime()) {
      long offset = timeSync.getUTCOffset();
      Serial.printf("Time: %s %s (Offset: %ld)\n", timeSync.getDateString(),
                    timeSync.getTimeString(), offset);
    }

    // WebSocket client count
    Serial.printf("WebSocket Clients: %d\n",
            wsHandler ? (int)wsHandler->getClientCount() : 0);

  } else if (mode == NetworkMode::AP) {
    Serial.printf("WiFi: AP Mode - %s\n", AP_SSID);
    Serial.printf("Access at: http://%s/\n",
                  networkMgr.getLocalIP().toString().c_str());

    // In AP Mode, we can't sync time, but let's print what we have or a warning
    if (timeSync.isInitialized()) {
      long offset = timeSync.getUTCOffset();
      Serial.printf("Time: %s %s (Offset: %ld)\n", timeSync.getDateString(),
                    timeSync.getTimeString(), offset);
    } else {
      Serial.println("Time: Not Synchronized (Requires WiFi Connection)");
    }

    // WebSocket client count (AP Mode)
    Serial.printf("WebSocket Clients: %d\n",
            wsHandler ? (int)wsHandler->getClientCount() : 0);

  } else {
    Serial.println("WiFi: Not connected");
  }

  // WiFi Status LED Control
  // Connected (STA) or AP active -> Solid ON
  // Disconnected -> Blink 1Hz (500ms ON, 500ms OFF)
  if (networkMgr.isConnected() || networkMgr.getMode() == NetworkMode::AP) {
    gpio_set_level(static_cast<gpio_num_t>(PIN_WIFI_LED), 1);
  } else {
    static unsigned long lastBlinkTime = 0;
    static bool ledState = false;
    unsigned long currentMillis = millis();

    if (currentMillis - lastBlinkTime >= 500) {
      lastBlinkTime = currentMillis;
      ledState = !ledState;
      gpio_set_level(static_cast<gpio_num_t>(PIN_WIFI_LED), ledState ? 1 : 0);
    }
  }

  Serial.print("Meat Temp:    ");
  if (Temperature::isValidTemp(sensorData.meatTemp)) {
    Serial.printf("%.1f°F\n", sensorData.meatTemp);
  } else {
    Serial.println("No Probe");
  }

  Serial.print("Pit Temp:     ");
  if (Temperature::isValidTemp(sensorData.pitTemp)) {
    Serial.printf("%.1f°F\n", sensorData.pitTemp);
  } else {
    Serial.println("No Probe");
  }

  Serial.printf("Setpoint:     %.0f°F\n", ctrlState.setpoint);
  Serial.printf("PID Output:   %.1f (0-255)\n", ctrlState.pidOutput);
  Serial.printf("Fan Speed:    %d%%\n", ctrlState.fanPercent);
  Serial.printf("Fan Duty:     %d (0-255)\n", fanActuator.getDutyCycle());
  Serial.println("-------------------------------------------\n");

  unsigned long now = millis();
  if (now - lastHistorySnapshotMs >= kHistorySnapshotIntervalMs) {
    saveHistorySnapshot(false);
    lastHistorySnapshotMs = now;
  }

  delay(1000);
}

extern "C" void app_main() {
  setup();
  while (true) {
    loop();
  }
}
