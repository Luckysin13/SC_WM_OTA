#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include "config/hardware_config.h"
#include "compat/compat.h"

// =============================================================================
// TEMPERATURE SENSOR CLASS
// =============================================================================
// Handles temperature reading from thermistors via ADS1015 12-bit ADC
// Uses Steinhart-Hart equation for accurate temperature calculation
// =============================================================================

class Temperature {
private:
  // Calculate temperature from ADC value using Steinhart-Hart equation
  // Returns temperature in Fahrenheit
  float calculateTemperature(float adcValue);

  bool initI2C();
  bool writeRegister(uint8_t reg, uint16_t value);
  bool readRegister(uint8_t reg, uint16_t &value);
  int16_t readAdcChannel(uint8_t channel);

public:
  // Initialize ADC with specified gain
  bool begin();

  // Read pit temperature probe (ADC channel 0)
  // Returns smoothed temperature in Fahrenheit
  float readPitTemp();

  // Read meat temperature probe (ADC channel 1)
  // Returns smoothed temperature in Fahrenheit
  float readMeatTemp();

  // Check if a temperature value is within valid range
  static bool isValidTemp(float temp);

  // Set calibration offsets
  void setPitOffset(int offset) { pitOffset = offset; }
  void setMeatOffset(int offset) { meatOffset = offset; }

  // Get calibration offsets
  int getPitOffset() const { return pitOffset; }
  int getMeatOffset() const { return meatOffset; }

private:
  int pitOffset = 0;
  int meatOffset = 0;

  // Smoothing state
  float smoothedPit = -999.0f;
  float smoothedMeat = -999.0f;
  const float SMOOTHING_FACTOR = 0.2f; // 20% weight to new reading
  const int OVERSAMPLE_COUNT = 16;
};

#endif // TEMPERATURE_H
