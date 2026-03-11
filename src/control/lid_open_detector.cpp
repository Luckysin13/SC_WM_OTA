#include "lid_open_detector.h"

LidOpenDetector::LidOpenDetector(float dropThreshold, uint32_t windowMs,
                                 uint32_t recoveryTimeoutMs)
    : dropThreshold(dropThreshold), windowMs(windowMs),
      recoveryTimeoutMs(recoveryTimeoutMs) {
  for (int i = 0; i < 15; i++)
    tempBuffer[i] = 0.0f;
}

bool LidOpenDetector::update(float currentTemp, double setpoint) {
  unsigned long now = millis();

  // Initialize baseline if needed
  if (baselineTemp < -500.0f) {
    baselineTemp = currentTemp;
    lastSampleTime = now;
    tempBuffer[bufferIdx] = currentTemp;
    bufferCount = 1;
    return false;
  }

  // Sample every 2 seconds for the window tracking
  if (now - lastSampleTime >= 2000) {
    bufferIdx = (bufferIdx + 1) % 15;
    tempBuffer[bufferIdx] = currentTemp;
    if (bufferCount < 15)
      bufferCount++;
    lastSampleTime = now;

    if (!lidOpen) {
      // Check for sudden drop over the window
      // The oldest point in the buffer is (bufferIdx + 1) % bufferCount
      int oldestIdx = (bufferCount < 15) ? 0 : (bufferIdx + 1) % 15;
      float drop = tempBuffer[oldestIdx] - currentTemp;

      // Trigger if drop exceeds threshold and we are within 20 degrees of
      // setpoint (avoid false positives on warmup) Or just if we are above room
      // temp. Let's stick to the drop threshold.
      if (drop >= dropThreshold && currentTemp < (setpoint + 10)) {
        lidOpen = true;
        openStartTime = now;
        Serial.printf("[Lid] Open detected! Drop: %.1fF in %ds\n", drop,
                      (bufferCount * 2));
      }
    }
  }

  // Check for recovery
  if (lidOpen) {
    // Recovery condition 1: Temperature starts rising significantly
    // (Wait at least 30 seconds since opening to avoid noise)
    if (now - openStartTime > 30000) {
      // If temp rises 2 degrees from the lowest point seen since opening?
      // Or just if it's within 2 degrees of previous reading and rising.
      // Simplified: If it rises 3 degrees from where it was when we last
      // checked.
      if (currentTemp > lastTemp + 1.0f) {
        // Potentially closed, but let's be sure.
        // For now, let's use a timeout or manual closure if needed,
        // but let's try a simple "stabilized or rising" check.
      }
    }

    // Recovery condition 2: Timeout
    if (now - openStartTime > recoveryTimeoutMs) {
      lidOpen = false;
      Serial.println("[Lid] Recovery timeout. Resuming control.");
    }

    // Manual recovery if temp exceeds setpoint
    if (currentTemp >= setpoint - 2.0f) {
      lidOpen = true; // Wait, if it's back to setpoint it's definitely closed
      lidOpen = false;
      Serial.println("[Lid] Temperature recovered. Resuming control.");
    }
  }

  lastTemp = currentTemp;
  return lidOpen;
}
