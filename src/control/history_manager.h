#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include "compat/compat.h"
#include <vector>

// =============================================================================
// HISTORY DATA POINT
// =============================================================================
struct HistoryPoint {
  uint32_t timestamp; // Seconds since boot or epoch
  int16_t pitTemp;
  int16_t meatTemp;
  int16_t setpoint;
  uint8_t fanPercent;
};

// =============================================================================
// HISTORY MANAGER CLASS
// =============================================================================
// Manages a RAM-based buffer of historical readings.
// Stores points only when significant changes occur to save memory.
// =============================================================================

class HistoryManager {
private:
  static const size_t MAX_POINTS = 1200; // ~20 hours at 1pt/min, ~19KB RAM
  std::vector<HistoryPoint> buffer;

  // Change detection thresholds
  static const int16_t THRESHOLD_TEMP = 1;
  static const uint8_t THRESHOLD_FAN = 5;
  static const uint32_t MAX_INTERVAL_MS = 60000; // 1 minute

  unsigned long lastPointTime = 0;
  HistoryPoint lastStoredPoint = {0, -999, -999, -999, 255};

public:
  HistoryManager();

  // Add a point if it meets change criteria or time interval
  void addPoint(int16_t pit, int16_t meat, int16_t set, uint8_t fan);

  // Get all points as a JSON array string
  String getHistoryJSON() const;

  // Clear all history
  void clear();

  // Get current point count
  size_t getCount() const { return buffer.size(); }
};

#endif // HISTORY_MANAGER_H
