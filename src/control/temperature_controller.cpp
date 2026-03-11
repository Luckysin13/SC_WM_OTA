#include "temperature_controller.h"
#include "compat/compat.h"

// =============================================================================
// TEMPERATURE CONTROLLER (PID) IMPLEMENTATION
// =============================================================================
// Preserves exact manual PID logic from original code (lines 521-556)
// =============================================================================

void TemperatureController::begin() {
    Serial.println("Temperature controller initialized");
}

void TemperatureController::setTunings(double newKp, double newKi, double newKd) {
    kp = newKp;
    ki = newKi;
    kd = newKd;
}

double TemperatureController::compute(double input, ControllerState& state) {
    // Calculate error
    double error = state.setpoint - input;

    // Integral term with anti-windup (saturation limits)
    state.integral += ki * error;
    if (state.integral > 255) state.integral = 255;
    if (state.integral < 0) state.integral = 0;

    // Derivative term (change in input, not error)
    double derivative = input - state.lastInput;
    state.lastInput = input;

    // Compute PID output
    state.pidOutput = kp * error + state.integral - kd * derivative;

    // Output saturation limits
    if (state.pidOutput > 255) state.pidOutput = 255;
    if (state.pidOutput < 0) state.pidOutput = 0;

    return state.pidOutput;
}

void TemperatureController::getTunings(double& outKp, double& outKi, double& outKd) const {
    outKp = kp;
    outKi = ki;
    outKd = kd;
}
