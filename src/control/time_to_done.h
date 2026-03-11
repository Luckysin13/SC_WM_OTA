#ifndef TIME_TO_DONE_H
#define TIME_TO_DONE_H

#include "compat/compat.h"
#include <cmath>

// =============================================================================
// TIME TO DONE PREDICTOR CLASS
// =============================================================================
// Estimates time remaining until meat reaches target temperature
// Uses exponential moving average of temperature rise rate
// =============================================================================

class TimeToDonePredictor {
private:
  // Temperature samples for rate calculation
  static constexpr int SAMPLE_WINDOW = 5;  // Number of samples for EMA
  float temperatureSamples[SAMPLE_WINDOW]; // Circular buffer of temperatures
  uint32_t timestampSamples[SAMPLE_WINDOW]; // Circular buffer of timestamps
  int sampleIndex = 0;
  int samplesCollected = 0;

  // Configuration
  static constexpr float EMA_ALPHA = 0.3f; // Exponential moving average factor
  static constexpr float MIN_RATE_THRESHOLD = 0.05f; // Min temp rise per min (F/min)
  static constexpr float MAX_RATE_THRESHOLD = 2.0f;  // Max temp rise per min (F/min)
  static constexpr uint32_t MIN_SAMPLE_INTERVAL = 30000; // 30 seconds between samples

  float lastCalculatedRate = 0.0f; // F per minute
  uint32_t lastSampleTime = 0;

public:
  // Initialize the predictor
  void begin();

  // Add a new temperature sample
  void addSample(float meatTemp, uint32_t currentTimeMs);

  // Calculate estimated time to done (returns string format "2h 15m" or "N/A")
  // Returns true if estimate is valid
  bool estimateTimeToDone(float currentMeatTemp, float targetMeatTemp,
                         int &outHours, int &outMinutes);

  // Get the current temperature rise rate (°F per minute)
  float getCurrentRate() const;

  // Reset predictor state
  void reset();

private:
  // Calculate rate of temperature rise (°F/minute)
  float calculateRate();
};

#endif // TIME_TO_DONE_H
