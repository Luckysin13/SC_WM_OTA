#include "pid_autotuner.h"

PIDAutotuner::PIDAutotuner() {}

void PIDAutotuner::start(double setpoint, double currentOutput) {
  this->setpoint = setpoint;
  this->baseOutput = currentOutput;
  this->state = State::TUNING;
  this->cycleCount = 0;
  this->lastSwitchTime = millis();
  this->pkMax = -1000;
  this->pkMin = 1000;
  Serial.printf("[Autotune] Started. Setpoint: %.1f, Base Output: %.1f\n",
                setpoint, baseOutput);
}

void PIDAutotuner::stop() {
  state = State::IDLE;
  Serial.println("[Autotune] Stopped manually.");
}

double PIDAutotuner::update(double currentTemp) {
  if (state != State::TUNING)
    return baseOutput;

  unsigned long now = millis();
  double output = baseOutput;

  // Relay logic
  if (currentTemp < setpoint) {
    output = baseOutput + stepSize;
  } else {
    output = baseOutput - stepSize;
  }

  // Clip output
  if (output > 255)
    output = 255;
  if (output < 0)
    output = 0;

  // Track peaks for amplitude and period
  if (currentTemp > pkMax)
    pkMax = currentTemp;
  if (currentTemp < pkMin)
    pkMin = currentTemp;

  // Detect crossing (relay switch)
  static bool lastAbove = false;
  bool currentAbove = (currentTemp > setpoint);

  if (currentAbove != lastAbove) {
    // We crossed setpoint
    unsigned long cyclePeriod = now - lastSwitchTime;

    // We consider a half cycle done. A full cycle is two crossings of the same
    // direction. Let's count full cycles (4 crossings).
    cycleCount++;

    if (cycleCount >= 4) { // Enough data for stability?
      double amplitude = (pkMax - pkMin) / 2.0;
      double periodSeconds =
          (double)(now - lastSwitchTime) * 2.0 / 1000.0; // Estimate full period

      if (amplitude > 0.5) { // Ensure we have a real oscillation
        calculateZN(amplitude, periodSeconds);
        state = State::COMPLETE;
        Serial.printf("[Autotune] Complete! Amp: %.2f, Period: %.1fs\n",
                      amplitude, periodSeconds);
      } else {
        // If amplitude is too small, maybe we need more cycles or larger steps
        if (cycleCount > 10) {
          state = State::FAILED;
          Serial.println("[Autotune] Failed: Low amplitude oscillation.");
        }
      }

      // Note: In a real implementation we might want to average several cycles.
    }

    lastSwitchTime = now;
    lastAbove = currentAbove;
    // Reset peaks for next cycle tracking?
    // Actually ZN uses the stable oscillation amplitude.
  }

  return output;
}

void PIDAutotuner::calculateZN(double amplitude, double periodSeconds) {
  // Ultimate Gain Ku = (4 * stepSize) / (pi * amplitude)
  double Ku = (4.0 * stepSize) / (M_PI * amplitude);
  double Tu = periodSeconds;

  // Ziegler-Nichols PID
  Kp = 0.6 * Ku;
  Ki = 2.0 * Kp / Tu;
  Kd = Kp * Tu / 8.0;

  Serial.printf(
      "[Autotune] Calculated Tunings -> Kp: %.2f, Ki: %.4f, Kd: %.2f\n", Kp, Ki,
      Kd);
}

void PIDAutotuner::getResults(double &kp, double &ki, double &kd) const {
  kp = Kp;
  ki = Ki;
  kd = Kd;
}
