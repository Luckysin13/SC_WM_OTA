#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

// =============================================================================
// HARDWARE CONFIGURATION
// =============================================================================
// Pin definitions, PWM settings, and thermistor constants for ESP32 smoker
// controller hardware
// =============================================================================

// =============================================================================
// GPIO PIN DEFINITIONS
// =============================================================================
#define PIN_RESET_SSID 14 // GPIO for factory reset (pull LOW to reset)
#define PIN_FAN_PWM 2     // GPIO for fan PWM output
#define PIN_WIFI_LED 18   // GPIO for WiFi status LED

// =============================================================================
// I2C CONFIGURATION
// =============================================================================
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define I2C_PORT_NUM 0
#define I2C_FREQUENCY_HZ 100000

// =============================================================================
// PWM CONFIGURATION (LEDC)
// =============================================================================
#define PWM_CHANNEL 0      // LEDC channel for PWM
#define PWM_FREQUENCY 1000 // PWM frequency in Hz
#define PWM_RESOLUTION 8   // 8-bit resolution (0-255)
#define FAN_MIN_DUTY 90    // Minimum duty cycle to start fan motor

// =============================================================================
// THERMISTOR CONFIGURATION (Steinhart-Hart Equation)
// =============================================================================
#define SERIES_RESISTOR 10500     // Series resistor in ohms
#define NOMINAL_RESISTANCE 200000 // Thermistor nominal resistance at 25°C
#define NOMINAL_TEMPERATURE 25    // Temperature for nominal resistance (°C)
#define B_COEFFICIENT 3899        // Beta coefficient of thermistor

// =============================================================================
// TEMPERATURE VALIDATION
// =============================================================================
#define TEMP_MIN_VALID 32  // Minimum valid temperature (°F)
#define TEMP_MAX_VALID 460 // Maximum valid temperature (°F)

// =============================================================================
// ADC CONFIGURATION
// =============================================================================
// ADS1015 12-bit ADC gain setting
// GAIN_ONE = +/- 4.096V range
#define ADC_GAIN GAIN_ONE

#endif // HARDWARE_CONFIG_H
