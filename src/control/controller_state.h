#ifndef CONTROLLER_STATE_H
#define CONTROLLER_STATE_H

#include "config/control_config.h"

// =============================================================================
// CONTROLLER STATE STRUCTURE
// =============================================================================
// Holds PID controller state including setpoint, output, and internal variables
// =============================================================================

struct ControllerState {
  double setpoint = DEFAULT_SETPOINT; // Target temperature (°F)
  double pidOutput = 0;               // PID output (0-255)
  int fanPercent = 0;                 // Fan speed percentage (0-100)

  // PID internal state (for manual PID implementation)
  double integral = 0;  // Integral accumulator
  double lastInput = 0; // Last input for derivative calculation

  bool autotuneActive = false; // Is autotuning in progress?

  double meatSetpoint = 195.0;     // Target meat temperature (°F)
  double keepWarmSetpoint = 160.0; // Target pit temperature during Keep Warm
  bool keepWarmEnabled = false;    // Is Ramp-to-Done (Keep Warm) active?

  bool fanAuto = true; // Auto fan control enabled

  // Reset PID internal state (call when setpoint changes significantly)
  void reset() {
    integral = 0;
    lastInput = 0;
    pidOutput = 0;
    fanPercent = 0;
    autotuneActive = false;
  }
};

#endif // CONTROLLER_STATE_H
