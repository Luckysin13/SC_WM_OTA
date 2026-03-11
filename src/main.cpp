// =============================================================================
// Smoker Controller - Phase 4: AP Configuration Mode
// =============================================================================
// Adds Access Point mode for initial WiFi configuration
// Automatic fallback to AP mode if STA connection fails
// Complete modular refactoring from original 699-line monolithic file
// =============================================================================

#include "compat/compat.h"
#include <cstring>
#include "driver/gpio.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
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
WebSocketHandler *wsHandler = nullptr;

OTAUpdater otaUpdater;

// AP mode handler (pointer to manage lifecycle)
APModeHandler *apHandler = nullptr;

// STA mode handler (pointer to manage lifecycle)
STAModeHandler *staHandler = nullptr;

namespace {
QueueHandle_t s_tzQueue = nullptr;

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

// =============================================================================
// WiFi LED TASK
// =============================================================================
void wifiLedTask(void *param) {
  (void)param;
  
  // Removed task start print to reduce serial spam
  
  bool ledState = false;
  unsigned long lastTime = millis();
  bool lastStateWasSolid = false;
  bool debugPrinted = false;
  int loopCount = 0;
  unsigned long lastWifiCheck = 0;
  bool isStaConnected = false;
  
  while (1) {
    wifi_mode_t wifiMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&wifiMode);
    
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
    
    // STA connected: solid ON
    if (isStaConnected && wifiMode == WIFI_MODE_STA) {
      gpio_set_level(static_cast<gpio_num_t>(PIN_WIFI_LED), 1);
      if (!lastStateWasSolid) {
        // Removed print
        lastStateWasSolid = true;
        debugPrinted = false;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // AP active or APSTA without STA connection: blink at 500ms
    if (wifiMode == WIFI_MODE_AP || (wifiMode == WIFI_MODE_APSTA && !isStaConnected)) {
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
    
    // APSTA with STA connected: solid ON
    if (isStaConnected && wifiMode == WIFI_MODE_APSTA) {
      gpio_set_level(static_cast<gpio_num_t>(PIN_WIFI_LED), 1);
      if (!lastStateWasSolid) {
        // Removed print
        lastStateWasSolid = true;
        debugPrinted = false;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
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
  // Initialize serial communication
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n===========================================");
  Serial.println("Smoker Controller - Phase 4");
  Serial.println("Complete Modular Architecture");
  Serial.println("===========================================\n");

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
    wsHandler = new WebSocketHandler(stateCoord);
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
    staHandler = new STAModeHandler(server, storage, networkMgr);
    staHandler->setupRoutes();

    // Initialize NTP time sync with stored timezone
    if (storageReady) {
      String savedTz = storage.loadTimezone();
      timeSync.begin(savedTz.c_str());
    }

  } else if (httpServerStarted && mode == NetworkMode::AP) {
    // AP Mode: Configuration portal
    Serial.println("\n[OK] Network initialized in AP mode");
    if (!storageReady) {
      Serial.println("[WARN] LittleFS unavailable; web UI may not load");
    }

    // Setup web server routes for configuration
    apHandler = new APModeHandler(server, storage, networkMgr);
    apHandler->setupRoutes();

  } else if (mode != NetworkMode::NONE) {
    // HTTP server failed to start but network is up
    Serial.println("\n[ERROR] HTTP server failed but network is available");
  } else {
    Serial.println("\n[ERROR] Network initialization failed");
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

  // Initialize display state
  ControllerState &ctrlState = stateCoord.getControllerMutable();
  ctrlState.setpoint = DEFAULT_SETPOINT;

  DisplayState &display = stateCoord.getDisplayMutable();
  display.updateSetpoint(ctrlState.setpoint);
  display.pitOffset = String(pitOffset);
  display.meatOffset = String(meatOffset);
  display.kp = String(kp, 2);
  display.ki = String(ki, 2);
  display.kd = String(kd, 2);
  display.timezone = tz;

  // Load settings for Ramp-to-Done
  ctrlState.meatSetpoint = storage.loadMeatSetpoint();
  ctrlState.keepWarmSetpoint = storage.loadKeepWarmSetpoint();
  display.meatSetpoint = String((int)ctrlState.meatSetpoint);
  display.keepWarmSetpoint = String((int)ctrlState.keepWarmSetpoint);

  Serial.printf("[OK] Default setpoint: %.0f°F\n", ctrlState.setpoint);

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
                                 : String(OTAUpdater::CURRENT_VERSION);
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
      delay(1000);
      ESP::restart();
    }
  }

  // Read temperature sensors
  SensorData sensorData;
  sensorData.pitTemp = tempSensor.readPitTemp();
  sensorData.meatTemp = tempSensor.readMeatTemp();
  stateCoord.updateSensors(sensorData);

  // Get controller state (mutable for PID computation)
  ControllerState &ctrlState = stateCoord.getControllerMutable();

  // Update lid detection
  ctrlState.lidOpen =
      lidDetector.update(sensorData.pitTemp, ctrlState.setpoint);

  // Run PID control loop (but skip if lid is open)
  double pidOutput = 0;
  if (ctrlState.lidOpen) {
    // If lid is open, we "halt" the PID output but keep lastInput updated to
    // avoid jumps
    ctrlState.lastInput = sensorData.pitTemp;
    pidOutput = 0;

    // Cancel autotune if lid opens
    if (ctrlState.autotuneActive) {
      ctrlState.autotuneActive = false;
      autotuner.stop();
      Serial.println("[Autotune] Cancelled: Lid opened.");
    }
  } else if (ctrlState.autotuneActive) {
    // Run Autotuner relay logic
    pidOutput = autotuner.update(sensorData.pitTemp);
    ctrlState.pidOutput = pidOutput; // For visibility

    // Check if finished
    if (autotuner.getState() == PIDAutotuner::State::COMPLETE) {
      double newKp, newKi, newKd;
      autotuner.getResults(newKp, newKi, newKd);

      // Apply new tunings
      pidController.setTunings(newKp, newKi, newKd);
      storage.savePIDTunings(newKp, newKi, newKd);

      // Update display state immediately
      DisplayState &display = stateCoord.getDisplayMutable();
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

  // Ramp-to-Done (Keep Warm) Logic
  if (ctrlState.keepWarmEnabled &&
      Temperature::isValidTemp(sensorData.meatTemp)) {
    if (sensorData.meatTemp >= (ctrlState.meatSetpoint + 0.5)) {
      if (ctrlState.setpoint != ctrlState.keepWarmSetpoint) {
        Serial.printf("[KeepWarm] Meat reached target (%.1f). Dropping pit "
                      "setpoint to %.1f\n",
                      sensorData.meatTemp, ctrlState.keepWarmSetpoint);
        ctrlState.setpoint = ctrlState.keepWarmSetpoint;
        // Reset PID for new lower target
        ctrlState.reset();
      }
    }
  }

  // Apply fan output (with minimum duty cycle enforcement)
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

  // Update display state for WebSocket transmission (Both AP and STA modes)
  if (networkMgr.getMode() != NetworkMode::NONE) {
    DisplayState &display = stateCoord.getDisplayMutable();

    // Update AP mode flag
    display.isAP = (networkMgr.getMode() == NetworkMode::AP);

    // WiFi status details
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

    // Check for changes and notify WebSocket clients if needed
    display.updateSetpoint(ctrlState.setpoint);
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

    stateCoord.updateDisplay();
  }

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

  delay(1000);
}

extern "C" void app_main() {
  setup();
  while (true) {
    loop();
  }
}
