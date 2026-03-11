#include "fan_actuator.h"
#include "compat/compat.h"
#include "driver/ledc.h"

// =============================================================================
// FAN ACTUATOR IMPLEMENTATION
// =============================================================================

bool FanActuator::begin() {
    // Configure LEDC timer
    ledc_timer_config_t timer_config{};
    timer_config.duty_resolution = static_cast<ledc_timer_bit_t>(PWM_RESOLUTION);
    timer_config.freq_hz = PWM_FREQUENCY;
    timer_config.speed_mode = LEDC_HIGH_SPEED_MODE;
    timer_config.timer_num = LEDC_TIMER_0;
    ledc_timer_config(&timer_config);

    // Configure LEDC channel
    ledc_channel_config_t channel_config{};
    channel_config.channel = static_cast<ledc_channel_t>(PWM_CHANNEL);
    channel_config.duty = 0;
    channel_config.gpio_num = PIN_FAN_PWM;
    channel_config.speed_mode = LEDC_HIGH_SPEED_MODE;
    channel_config.hpoint = 0;
    channel_config.timer_sel = LEDC_TIMER_0;
    ledc_channel_config(&channel_config);

    // Initialize fan to off state
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, static_cast<ledc_channel_t>(PWM_CHANNEL), 0);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, static_cast<ledc_channel_t>(PWM_CHANNEL));
    currentDutyCycle = 0;

    Serial.println("Fan actuator initialized successfully");
    return true;
}

void FanActuator::setDutyCycle(int dutyCycle) {
    // Enforce minimum duty cycle or turn off
    // Fan motor requires minimum duty cycle to overcome starting torque
    if (dutyCycle >= FAN_MIN_DUTY) {
        currentDutyCycle = dutyCycle;
    } else {
        currentDutyCycle = 0;
    }

    // Constrain to valid range
    if (currentDutyCycle > 255) {
        currentDutyCycle = 255;
    }
    if (currentDutyCycle < 0) {
        currentDutyCycle = 0;
    }

    ledc_set_duty(LEDC_HIGH_SPEED_MODE, static_cast<ledc_channel_t>(PWM_CHANNEL), currentDutyCycle);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, static_cast<ledc_channel_t>(PWM_CHANNEL));
}

void FanActuator::setSpeedPercent(int percent) {
    // Constrain input to 0-100%
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // Convert percentage to duty cycle
    // Map 0-100% to FAN_MIN_DUTY-255 range, or 0 if percent is 0
    int dutyCycle;
    if (percent == 0) {
        dutyCycle = 0;
    } else {
        dutyCycle = FAN_MIN_DUTY + ((percent * (255 - FAN_MIN_DUTY)) / 100);
    }

    setDutyCycle(dutyCycle);
}

int FanActuator::getSpeedPercent() const {
    // Convert current duty cycle back to percentage
    if (currentDutyCycle == 0) {
        return 0;
    }

    return ((currentDutyCycle - FAN_MIN_DUTY) * 100) / (255 - FAN_MIN_DUTY);
}

int FanActuator::getDutyCycle() const {
    return currentDutyCycle;
}
