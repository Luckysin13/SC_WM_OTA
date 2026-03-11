#ifndef PID_AUTOTUNER_H
#define PID_AUTOTUNER_H

#include "compat/compat.h"

class PIDAutotuner {
public:
  enum class State { IDLE, TUNING, COMPLETE, FAILED };

  PIDAutotuner();

  void start(double setpoint, double currentOutput);
  void stop();

  // Updates the tuner with the latest temperature
  // Returns the fan output to apply (0-255)
  double update(double currentTemp);

  State getState() const { return state; }
  void getResults(double &kp, double &ki, double &kd) const;

  // Reset completed/failed state back to idle
  void reset() { state = State::IDLE; }

private:
  State state = State::IDLE;
  double setpoint = 0;
  double baseOutput = 0;
  double stepSize = 40; // Relay step (+/- 40 from base)

  // Oscillation tracking
  int cycleCount = 0;
  unsigned long lastSwitchTime = 0;
  double pkMax = -1000;
  double pkMin = 1000;

  // Results
  double Kp = 0, Ki = 0, Kd = 0;

  void calculateZN(double amplitude, double periodSeconds);
};

#endif // PID_AUTOTUNER_H
