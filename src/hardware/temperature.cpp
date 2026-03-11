#include "temperature.h"
#include "compat/compat.h"
#include "driver/i2c.h"
#include <math.h>

// =============================================================================
// TEMPERATURE SENSOR IMPLEMENTATION
// =============================================================================

bool Temperature::begin() {
  if (!initI2C()) {
    Serial.println("Failed to initialize I2C for ADS1015");
    return false;
  }

  Serial.println("ADS1015 ADC initialized successfully");
  return true;
}

bool Temperature::initI2C() {
  static bool initialized = false;
  if (initialized) {
    return true;
  }

  i2c_config_t conf{};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = static_cast<gpio_num_t>(I2C_SDA_PIN);
  conf.scl_io_num = static_cast<gpio_num_t>(I2C_SCL_PIN);
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = I2C_FREQUENCY_HZ;

  if (i2c_param_config(static_cast<i2c_port_t>(I2C_PORT_NUM), &conf) != ESP_OK) {
    return false;
  }
  if (i2c_driver_install(static_cast<i2c_port_t>(I2C_PORT_NUM), conf.mode, 0, 0, 0) != ESP_OK) {
    return false;
  }

  initialized = true;
  return true;
}

bool Temperature::writeRegister(uint8_t reg, uint16_t value) {
  uint8_t data[3] = {reg, static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF)};
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (0x48 << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write(cmd, data, sizeof(data), true);
  i2c_master_stop(cmd);
  esp_err_t err = i2c_master_cmd_begin(static_cast<i2c_port_t>(I2C_PORT_NUM), cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return err == ESP_OK;
}

bool Temperature::readRegister(uint8_t reg, uint16_t &value) {
  uint8_t data[2] = {0, 0};
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (0x48 << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (0x48 << 1) | I2C_MASTER_READ, true);
  i2c_master_read(cmd, data, sizeof(data), I2C_MASTER_LAST_NACK);
  i2c_master_stop(cmd);
  esp_err_t err = i2c_master_cmd_begin(static_cast<i2c_port_t>(I2C_PORT_NUM), cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  if (err != ESP_OK) {
    return false;
  }
  value = static_cast<uint16_t>((data[0] << 8) | data[1]);
  return true;
}

int16_t Temperature::readAdcChannel(uint8_t channel) {
  // ADS1015 config: single-shot, AINx to GND, gain +/-4.096V, 1600 SPS
  uint16_t config = 0x8000; // OS = 1 (start single conversion)
  switch (channel) {
    case 0: config |= 0x4000; break; // MUX = 100
    case 1: config |= 0x5000; break; // MUX = 101
    case 2: config |= 0x6000; break; // MUX = 110
    case 3: config |= 0x7000; break; // MUX = 111
    default: config |= 0x4000; break;
  }
  config |= 0x0200; // PGA = +/-4.096V
  config |= 0x0100; // MODE = single-shot
  config |= 0x0080; // DR = 1600 SPS
  config |= 0x0003; // Disable comparator

  if (!writeRegister(0x01, config)) {
    return 0;
  }

  delay(2);

  uint16_t raw = 0;
  if (!readRegister(0x00, raw)) {
    return 0;
  }

  // ADS1015 is 12-bit, left-justified
  return static_cast<int16_t>(raw >> 4);
}

float Temperature::calculateTemperature(float adcValue) {
  // Calculate resistance from ADC value
  // ADC range: 0-1650 counts for 12-bit ADS1015 at GAIN_ONE
  float resistance = (1650.0 / adcValue) - 1.0;
  resistance = SERIES_RESISTOR / resistance;

  // Steinhart-Hart equation for thermistor temperature calculation
  float steinhart = resistance / NOMINAL_RESISTANCE; // (R/Ro)
  steinhart = log(steinhart);                        // ln(R/Ro)
  steinhart /= B_COEFFICIENT;                        // 1/B * ln(R/Ro)
  steinhart += 1.0 / (NOMINAL_TEMPERATURE + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                       // Invert to get Kelvin
  steinhart -= 273.15;                               // Convert to Celsius

  // Convert Celsius to Fahrenheit
  return (steinhart * 9.0 / 5.0) + 32.0;
}

float Temperature::readPitTemp() {
  float totalAdc = 0;
  for (int i = 0; i < OVERSAMPLE_COUNT; i++) {
    totalAdc += static_cast<float>(readAdcChannel(0));
    delay(2); // Small delay to allow ADC capacitor to stabilize
  }

  float avgAdc = totalAdc / OVERSAMPLE_COUNT;
  float currentTemp = calculateTemperature(avgAdc) + (float)pitOffset;

  // EMA Filter: Smoothed = (New * Alpha) + (Old * (1 - Alpha))
  if (smoothedPit < -500.0f) {
    smoothedPit = currentTemp; // Initial value
  } else {
    smoothedPit = (currentTemp * SMOOTHING_FACTOR) +
                  (smoothedPit * (1.0f - SMOOTHING_FACTOR));
  }

  return smoothedPit;
}

float Temperature::readMeatTemp() {
  float totalAdc = 0;
  for (int i = 0; i < OVERSAMPLE_COUNT; i++) {
    totalAdc += static_cast<float>(readAdcChannel(1));
    delay(2);
  }

  float avgAdc = totalAdc / OVERSAMPLE_COUNT;
  float currentTemp = calculateTemperature(avgAdc) + (float)meatOffset;

  if (smoothedMeat < -500.0f) {
    smoothedMeat = currentTemp;
  } else {
    smoothedMeat = (currentTemp * SMOOTHING_FACTOR) +
                   (smoothedMeat * (1.0f - SMOOTHING_FACTOR));
  }

  return smoothedMeat;
}

bool Temperature::isValidTemp(float temp) {
  return (temp >= TEMP_MIN_VALID && temp <= TEMP_MAX_VALID);
}
