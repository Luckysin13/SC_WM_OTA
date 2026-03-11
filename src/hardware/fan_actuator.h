#ifndef FAN_ACTUATOR_H
#define FAN_ACTUATOR_H

#include "config/hardware_config.h"

// =============================================================================
// FAN ACTUATOR CLASS
// =============================================================================
// Controls fan motor via PWM using ESP32 LEDC peripheral
// Handles minimum duty cycle requirement for motor startup
// =============================================================================

class FanActuator {
private:
    int currentDutyCycle = 0;

public:
    // Initialize PWM channel and attach to output pin
    bool begin();

    // Set fan speed as percentage (0-100%)
    // Handles conversion to duty cycle and minimum speed enforcement
    void setSpeedPercent(int percent);

    // Set raw PWM duty cycle (0-255)
    // Values below FAN_MIN_DUTY will turn fan off
    void setDutyCycle(int dutyCycle);

    // Get current fan speed percentage (0-100%)
    int getSpeedPercent() const;

    // Get current raw duty cycle (0-255)
    int getDutyCycle() const;
};

#endif // FAN_ACTUATOR_H
