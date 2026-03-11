#include "time_to_done.h"

// =============================================================================
// TIME TO DONE PREDICTOR IMPLEMENTATION
// =============================================================================

void TimeToDonePredictor::begin() {
  // Initialize sample buffers
  for (int i = 0; i < SAMPLE_WINDOW; i++) {
    temperatureSamples[i] = 0.0f;
    timestampSamples[i] = 0;
  }
  sampleIndex = 0;
  samplesCollected = 0;
  lastCalculatedRate = 0.0f;
  lastSampleTime = 0;
}

void TimeToDonePredictor::addSample(float meatTemp, uint32_t currentTimeMs) {
  // Enforce minimum sample interval to avoid noise
  if (lastSampleTime != 0 && 
      (currentTimeMs - lastSampleTime) < MIN_SAMPLE_INTERVAL) {
    return;
  }

  // Add sample to circular buffer
  temperatureSamples[sampleIndex] = meatTemp;
  timestampSamples[sampleIndex] = currentTimeMs;

  sampleIndex = (sampleIndex + 1) % SAMPLE_WINDOW;
  if (samplesCollected < SAMPLE_WINDOW) {
    samplesCollected++;
  }

  lastSampleTime = currentTimeMs;

  // Recalculate rate if we have enough samples
  if (samplesCollected >= 2) {
    lastCalculatedRate = calculateRate();
  }
}

bool TimeToDonePredictor::estimateTimeToDone(float currentMeatTemp,
                                             float targetMeatTemp,
                                             int &outHours,
                                             int &outMinutes) {
  outHours = 0;
  outMinutes = 0;

  // Need at least 2 samples for rate calculation
  if (samplesCollected < 2) {
    return false;
  }

  // Check if meat has already reached target
  if (currentMeatTemp >= targetMeatTemp) {
    return false;
  }

  // Get current rate
  float rate = lastCalculatedRate;

  // Rate must be within valid range
  if (rate < MIN_RATE_THRESHOLD || rate > MAX_RATE_THRESHOLD) {
    return false;
  }

  // Calculate temperature delta
  float tempDelta = targetMeatTemp - currentMeatTemp;

  // Calculate time remaining in minutes
  float minutesRemaining = tempDelta / rate;

  // Convert to hours and minutes
  outHours = (int)(minutesRemaining / 60.0f);
  outMinutes = (int)(minutesRemaining - (outHours * 60.0f));

  // Clamp to reasonable values
  if (outHours > 99) {
    outHours = 99;
    outMinutes = 59;
  }

  return true;
}

float TimeToDonePredictor::getCurrentRate() const {
  return lastCalculatedRate;
}

void TimeToDonePredictor::reset() {
  begin(); // Reinitialize all members
}

float TimeToDonePredictor::calculateRate() {
  if (samplesCollected < 2) {
    return 0.0f;
  }

  // Get oldest and newest samples
  int oldestIndex = (sampleIndex + (SAMPLE_WINDOW - samplesCollected)) % SAMPLE_WINDOW;
  int newestIndex = (sampleIndex - 1 + SAMPLE_WINDOW) % SAMPLE_WINDOW;

  float tempDelta =
      temperatureSamples[newestIndex] - temperatureSamples[oldestIndex];
  uint32_t timeDeltaMs = timestampSamples[newestIndex] - timestampSamples[oldestIndex];

  // Handle edge case of same timestamp
  if (timeDeltaMs == 0) {
    return lastCalculatedRate;
  }

  // Convert to °F per minute
  float rate = (tempDelta / (float)timeDeltaMs) * 60000.0f;

  // Apply exponential moving average for smoothing
  if (lastCalculatedRate > 0.0f) {
    rate = (EMA_ALPHA * rate) + ((1.0f - EMA_ALPHA) * lastCalculatedRate);
  }

  return std::abs(rate); // Use absolute value to handle cooling
}
