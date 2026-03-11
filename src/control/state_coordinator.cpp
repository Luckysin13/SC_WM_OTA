#include "state_coordinator.h"
#include <algorithm>

// =============================================================================
// STATE COORDINATOR IMPLEMENTATION
// =============================================================================

// =============================================================================
// INITIALIZATION
// =============================================================================

void StateCoordinator::begin() {
}

// =============================================================================
// SENSOR DATA ACCESS
// =============================================================================

const SensorData &StateCoordinator::getSensors() const { return sensors; }

void StateCoordinator::updateSensors(const SensorData &data) { sensors = data; }

// =============================================================================
// CONTROLLER STATE ACCESS
// =============================================================================

const ControllerState &StateCoordinator::getController() const {
  return controller;
}

ControllerState &StateCoordinator::getControllerMutable() { return controller; }

void StateCoordinator::updateController(const ControllerState &state) {
  controller = state;
}

// =============================================================================
// DISPLAY STATE ACCESS
// =============================================================================

const DisplayState &StateCoordinator::getDisplay() const { return display; }

DisplayState &StateCoordinator::getDisplayMutable() { return display; }

void StateCoordinator::updateDisplay() {
  // Check if display state has changed
  if (display.hasChanged()) {
    // Record history point (HistoryManager handles thresholds internally)
    history.addPoint(
        (int16_t)round(sensors.pitTemp), (int16_t)round(sensors.meatTemp),
        (int16_t)round(controller.setpoint), (uint8_t)controller.fanPercent);

    // Mark as seen
    display.markClean();

    // Notify all observers (WebSocket clients)
    notifyObservers();
  }
}

// =============================================================================
// HISTORY ACCESS
// =============================================================================

HistoryManager &StateCoordinator::getHistory() { return history; }

// =============================================================================
// OBSERVER MANAGEMENT
// =============================================================================

void StateCoordinator::addObserver(IStateObserver *observer) {
  if (observer != nullptr) {
    observers.push_back(observer);
  }
}

void StateCoordinator::removeObserver(IStateObserver *observer) {
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}

void StateCoordinator::notifyObservers() {
  for (IStateObserver *observer : observers) {
    if (observer != nullptr) {
      observer->onStateChanged();
    }
  }
}
