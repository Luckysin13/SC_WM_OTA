#ifndef LID_OPEN_DETECTOR_H
#define LID_OPEN_DETECTOR_H

#include "compat/compat.h"

class LidOpenDetector {
public:
  LidOpenDetector(float dropThreshold = 5.0f, uint32_t windowMs = 30000,
                  uint32_t recoveryTimeoutMs = 300000);

  // Update detector with current temperature
  // Returns true if lid is detected as open
  bool update(float currentTemp, double setpoint);

  bool isLidOpen() const { return lidOpen; }
  void forceClose() { lidOpen = false; }

private:
  float dropThreshold;        // Degrees drop to trigger
  uint32_t windowMs;          // Time window for the drop
  uint32_t recoveryTimeoutMs; // Force recovery after this long

  bool lidOpen = false;
  float lastTemp = -999.0f;
  float baselineTemp = -999.0f;
  unsigned long lastUpdateTime = 0;
  unsigned long openStartTime = 0;

  // For tracking the drop over the window
  float tempBuffer[15]; // Store samples every 2 seconds for 30s window
  int bufferIdx = 0;
  int bufferCount = 0;
  unsigned long lastSampleTime = 0;
};

#endif // LID_OPEN_DETECTOR_H
