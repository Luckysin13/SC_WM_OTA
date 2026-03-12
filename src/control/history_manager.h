#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include "compat/compat.h"
#include <string>
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
  static const size_t MAX_POINTS = 1800; // 15 hours at 30s sampling
  std::vector<HistoryPoint> buffer;
  size_t head = 0;
  size_t count = 0;

  static const uint32_t SAMPLE_INTERVAL_MS = 30000; // 30 seconds

  unsigned long lastPointTime = 0;
  HistoryPoint lastStoredPoint = {0, -999, -999, -999, 255};
  bool dirty = false;

  const HistoryPoint &pointAt(size_t index) const;
  void appendPoint(const HistoryPoint &point);

public:
  HistoryManager();

  // Add a point if it meets change criteria or time interval
  void addPoint(int16_t pit, int16_t meat, int16_t set, uint8_t fan);

  // Get all points as a JSON array string
  String getHistoryJSON() const;

  // Get a chunk of historical points as JSON.
  String getHistoryChunkJSON(size_t start, size_t maxPoints,
                             int32_t utcOffsetSeconds) const;

  // Serialize or restore history checkpoints for software restarts.
  std::string serializeSnapshot() const;
  bool restoreSnapshot(const std::string &snapshotData);

  bool hasUnsavedChanges() const { return dirty; }
  void markSnapshotSaved() { dirty = false; }

  // Clear all history
  void clear();

  // Get current point count
  size_t getCount() const { return count; }
};

#endif // HISTORY_MANAGER_H
