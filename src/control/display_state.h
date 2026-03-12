#ifndef DISPLAY_STATE_H
#define DISPLAY_STATE_H

#include "config/control_config.h"
#include "utils/time_sync.h"
#include "compat/compat.h"
#include <math.h>

// =============================================================================
// DISPLAY STATE STRUCTURE
// =============================================================================
// Holds UI display values with change detection and JSON serialization
// Maps to boxValue0-6 from original code
// =============================================================================

struct DisplayState {
  static String escapeJson(const String &value) {
    std::string escaped;
    escaped.reserve(value.length() + 8);
    for (char ch : value.str()) {
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
          snprintf(buffer, sizeof(buffer), "\\u%04x",
                   static_cast<unsigned char>(ch));
          escaped += buffer;
        } else {
          escaped.push_back(ch);
        }
        break;
      }
    }
    return String(escaped);
  }

  // Temperature displays
  String meatTemp = "Meat";        // boxValue0
  String pitTemp = "Pit";          // boxValue1
  String setpoint = "225";         // boxValue2
  String fanSpeed = "---";         // boxValue3
  String meatSetpoint = "195";     // boxValue8
  String keepWarmSetpoint = "160"; // boxValue9

  // WiFi status
  String wifiSsid = "";
  String wifiRssi = "";
  String wifiIp = "";
  bool wifiConnected = false;

  // Calibration Offsets
  String pitOffset = "0";
  String meatOffset = "0";

  // PID Parameters
  String kp = String(PID_KP, 2);
  String ki = String(PID_KI, 2);
  String kd = String(PID_KD, 2);
  String timezone = "";

  // Alarm states (not fully implemented yet, prepared for Phase 5)
  String keepWarmAlarm = "false";    // boxValue4
  String pitTempLowAlarm = "false";  // boxValue5
  String doneAlarm = "false";        // boxValue6
  String lidDetectionAlarm = "false"; // boxValue7

  // Last values for change detection
  String lastMeatTemp = "0";
  String lastPitTemp = "0";
  String lastSetpoint = "0";
  String lastFanSpeed = "0";
  String lastKp = "0";
  String lastKi = "0";
  String lastKd = "0";
  String lastTimezone = "";
  String lastMeatSetpoint = "0";
  String lastKeepWarmSetpoint = "0";
  bool lastIsAP = false;
  bool lastLidOpen = false;
  bool lastAutotuneActive = false;
  String lastWifiSsid = "";
  String lastWifiRssi = "";
  String lastWifiIp = "";
  bool lastWifiConnected = false;
  bool lastFanAuto = true;

  // Flags
  bool isAP = false;
  bool lidOpen = false;
  bool autotuneActive = false;
  int autotuneState = 0; // 0: Idle, 1: Tuning, 2: Complete, 3: Failed
  bool fanAuto = true;

  // Fan speed smoothing
  float smoothedFan = -1.0f;
  const float FAN_SMOOTHING_FACTOR = 0.3f;

  // Check if any values have changed since last check
  bool hasChanged() const {
    return (meatTemp != lastMeatTemp) || (pitTemp != lastPitTemp) ||
           (setpoint != lastSetpoint) || (fanSpeed != lastFanSpeed) ||
           (kp != lastKp) || (ki != lastKi) || (kd != lastKd) ||
           (timezone != lastTimezone) || (isAP != lastIsAP) ||
           (meatSetpoint != lastMeatSetpoint) ||
           (keepWarmSetpoint != lastKeepWarmSetpoint) ||
           (lidOpen != lastLidOpen) || (autotuneActive != lastAutotuneActive) ||
           (wifiSsid != lastWifiSsid) || (wifiRssi != lastWifiRssi) ||
           (wifiIp != lastWifiIp) || (wifiConnected != lastWifiConnected) ||
           (fanAuto != lastFanAuto);
  }

  // Mark current values as "seen" (for change detection)
  void markClean() {
    lastMeatTemp = meatTemp;
    lastPitTemp = pitTemp;
    lastSetpoint = setpoint;
    lastFanSpeed = fanSpeed;
    lastKp = kp;
    lastKi = ki;
    lastKd = kd;
    lastTimezone = timezone;
    lastIsAP = isAP;
    lastMeatSetpoint = meatSetpoint;
    lastKeepWarmSetpoint = keepWarmSetpoint;
    lastLidOpen = lidOpen;
    lastAutotuneActive = autotuneActive;
    lastWifiSsid = wifiSsid;
    lastWifiRssi = wifiRssi;
    lastWifiIp = wifiIp;
    lastWifiConnected = wifiConnected;
    lastFanAuto = fanAuto;
  }

  // Convert to JSON for WebSocket transmission
  String toJSON() const {
    String json = "{";
    json += "\"boxValue0\":\"" + escapeJson(meatTemp) + "\",";
    json += "\"boxValue1\":\"" + escapeJson(pitTemp) + "\",";
    json += "\"boxValue2\":\"" + escapeJson(setpoint) + "\",";
    json += "\"boxValue3\":\"" + escapeJson(fanSpeed) + "\",";
    json += "\"pitOffset\":\"" + escapeJson(pitOffset) + "\",";
    json += "\"meatOffset\":\"" + escapeJson(meatOffset) + "\",";
    json += "\"kp\":\"" + escapeJson(kp) + "\",";
    json += "\"ki\":\"" + escapeJson(ki) + "\",";
    json += "\"kd\":\"" + escapeJson(kd) + "\",";
    json += "\"timezone\":\"" + escapeJson(timezone) + "\",";
    json += "\"isAP\":" + String(isAP ? "true" : "false") + ",";
    json += "\"lid\":" + String(lidOpen ? "true" : "false") + ",";
    json += "\"atActive\":" + String(autotuneActive ? "true" : "false") + ",";
    json += "\"atState\":" + String(autotuneState) + ",";
    json += "\"fanAuto\":" + String(fanAuto ? "true" : "false") + ",";

    extern TimeSync timeSync;
    json += "\"t\":" + String((int)time(nullptr)) + ",";
    json += "\"o\":" + String((int)timeSync.getUTCOffset()) + ",";

    json += "\"boxValue4\":\"" + escapeJson(keepWarmAlarm) + "\",";
    json += "\"boxValue5\":\"" + escapeJson(pitTempLowAlarm) + "\",";
    json += "\"boxValue6\":\"" + escapeJson(doneAlarm) + "\",";
    json += "\"boxValue7\":\"" + escapeJson(lidDetectionAlarm) + "\",";
    json += "\"boxValue8\":\"" + escapeJson(meatSetpoint) + "\",";
    json += "\"boxValue9\":\"" + escapeJson(keepWarmSetpoint) + "\",";

    json += "\"ssid\":\"" + escapeJson(wifiSsid) + "\",";
    json += "\"rssi\":" + wifiRssi + ",";
    json += "\"ip\":\"" + escapeJson(wifiIp) + "\",";
    json += "\"connected\":" + String(wifiConnected ? "true" : "false");
    json += "}";
    return json;
  }

  // Update temperature display with validation
  void updateMeatTemp(float temp, bool isValid) {
    if (isValid) {
      meatTemp = String(temp, 1);
    } else {
      meatTemp = "No Probe";
    }
  }

  void updatePitTemp(float temp, bool isValid) {
    if (isValid) {
      pitTemp = String(temp, 1);
    } else {
      pitTemp = "No Probe";
    }
  }

  // Update setpoint display
  void updateSetpoint(double value) { setpoint = String((int)value); }

  // Update fan speed display (EMA smoothing)
  void updateFanSpeed(int percent) {
    if (smoothedFan < 0.0f) {
      smoothedFan = static_cast<float>(percent);
    } else {
      smoothedFan = (static_cast<float>(percent) * FAN_SMOOTHING_FACTOR) +
                    (smoothedFan * (1.0f - FAN_SMOOTHING_FACTOR));
    }
    fanSpeed = String(static_cast<int>(round(smoothedFan)));
  }
};

#endif // DISPLAY_STATE_H
