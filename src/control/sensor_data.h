#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include "config/hardware_config.h"

// =============================================================================
// SENSOR DATA STRUCTURE
// =============================================================================
// Holds temperature sensor readings with validation
// =============================================================================

struct SensorData {
  float pitTemp = 0.0f;  // Pit (smoker chamber) temperature in °F
  float meatTemp = 0.0f; // Meat probe temperature in °F

  // Check if pit temperature reading is within valid range
  bool isPitTempValid() const {
    return (pitTemp >= TEMP_MIN_VALID && pitTemp <= TEMP_MAX_VALID);
  }

  // Check if meat temperature reading is within valid range
  bool isMeatTempValid() const {
    return (meatTemp >= TEMP_MIN_VALID && meatTemp <= TEMP_MAX_VALID);
  }

  // Check if both temperature readings are valid
  bool isValid() const { return isPitTempValid() && isMeatTempValid(); }
};

#endif // SENSOR_DATA_H
