#include "history_manager.h"
#include "utils/time_sync.h"
#include <stdint.h>
#include <time.h>

HistoryManager::HistoryManager() { buffer.reserve(MAX_POINTS); }

void HistoryManager::addPoint(int16_t pit, int16_t meat, int16_t set,
                              uint8_t fan) {
  unsigned long now = millis();

  // Determine if we should store this point
  bool significantChange =
      abs(pit - lastStoredPoint.pitTemp) >= THRESHOLD_TEMP ||
      abs(meat - lastStoredPoint.meatTemp) >= THRESHOLD_TEMP ||
      abs(set - lastStoredPoint.setpoint) >= THRESHOLD_TEMP ||
      abs((int)fan - (int)lastStoredPoint.fanPercent) >= THRESHOLD_FAN;

  bool intervalElapsed = (now - lastPointTime) >= MAX_INTERVAL_MS;

  if (significantChange || intervalElapsed || buffer.empty()) {
    // If buffer is full, remove oldest point
    if (buffer.size() >= MAX_POINTS) {
      buffer.erase(buffer.begin());
    }

    HistoryPoint p;
    p.timestamp = (uint32_t)time(nullptr); // Store as Unix timestamp
    p.pitTemp = pit;
    p.meatTemp = meat;
    p.setpoint = set;
    p.fanPercent = fan;

    buffer.push_back(p);
    lastStoredPoint = p;
    lastPointTime = now;
  }
}

String HistoryManager::getHistoryJSON() const {
  String json = "{\"type\":\"history\",\"data\":[";
  for (size_t i = 0; i < buffer.size(); i++) {
    if (i > 0) {
      json += ",";
    }
    json += "{";
    json += "\"t\":" + String((int)buffer[i].timestamp) + ",";
    json += "\"p\":" + String(buffer[i].pitTemp) + ",";
    json += "\"m\":" + String(buffer[i].meatTemp) + ",";
    json += "\"s\":" + String(buffer[i].setpoint) + ",";
    json += "\"f\":" + String(buffer[i].fanPercent);
    json += "}";
  }
  json += "]";

  extern TimeSync timeSync;
  json += ",\"offset\":" + String((int)timeSync.getUTCOffset());
  json += "}";
  return json;
}

void HistoryManager::clear() {
  buffer.clear();
  lastPointTime = 0;
  lastStoredPoint = {0, -999, -999, -999, 255};
}
