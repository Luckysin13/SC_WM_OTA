#ifndef CONTROL_CONFIG_H
#define CONTROL_CONFIG_H

// =============================================================================
// CONTROL CONFIGURATION
// =============================================================================
// PID tuning parameters and control setpoints for smoker temperature control
// =============================================================================

// =============================================================================
// PID TUNING PARAMETERS
// =============================================================================
#define PID_KP 10.0            // Proportional gain
#define PID_KI 0.05            // Integral gain
#define PID_KD 2.0             // Derivative gain

// =============================================================================
// CONTROL SETPOINTS
// =============================================================================
#define DEFAULT_SETPOINT 225   // Default temperature setpoint (°F)
#define PID_SAMPLE_TIME 1000   // PID sample time (milliseconds)

#endif // CONTROL_CONFIG_H
