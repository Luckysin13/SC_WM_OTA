#ifndef TEMPERATURE_CONTROLLER_H
#define TEMPERATURE_CONTROLLER_H

#include "config/control_config.h"
#include "control/controller_state.h"

// =============================================================================
// TEMPERATURE CONTROLLER (PID) CLASS
// =============================================================================
// Manual PID implementation for smoker temperature control
// =============================================================================

class TemperatureController {
private:
  double kp = PID_KP;
  double ki = PID_KI;
  double kd = PID_KD;

public:
  // Initialize controller with default tuning parameters
  void begin();

  // Set PID tuning parameters
  void setTunings(double kp, double ki, double kd);

  // Compute PID output based on current input and setpoint
  // Updates the ControllerState with new output value
  // Returns the computed output (0-255)
  double compute(double input, ControllerState &state);

  // Get current tuning parameters
  void getTunings(double &kp, double &ki, double &kd) const;
};

#endif // TEMPERATURE_CONTROLLER_H
