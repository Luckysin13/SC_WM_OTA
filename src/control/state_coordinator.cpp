#include "state_coordinator.h"
#include "utils/time_sync.h"
#include <algorithm>
#include <cmath>

extern TimeSync timeSync;

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

SensorData StateCoordinator::getSensors() const {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return sensors;
}

void StateCoordinator::updateSensors(const SensorData &data) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  sensors = data;
}

// =============================================================================
// CONTROLLER STATE ACCESS
// =============================================================================

ControllerState StateCoordinator::getController() const {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return controller;
}

void StateCoordinator::updateController(const ControllerState &state) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  controller = state;
}

// =============================================================================
// DISPLAY STATE ACCESS
// =============================================================================

DisplayState StateCoordinator::getDisplay() const {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return display;
}

void StateCoordinator::updateDisplay() {
  std::vector<IStateObserver *> observerSnapshot;
  bool displayChanged = false;

  {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    history.addPoint(
        (int16_t)round(sensors.pitTemp), (int16_t)round(sensors.meatTemp),
        (int16_t)round(controller.setpoint), (uint8_t)controller.fanPercent);

    displayChanged = display.hasChanged();
    if (!displayChanged) {
      return;
    }

    display.markClean();
    observerSnapshot = observers;
  }

  for (IStateObserver *observer : observerSnapshot) {
    if (observer != nullptr) {
      observer->onStateChanged();
    }
  }
}

// =============================================================================
// HISTORY ACCESS
// =============================================================================

String StateCoordinator::getHistoryJSON() const {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return history.getHistoryJSON();
}

String StateCoordinator::getHistoryChunkJSON(size_t start,
                                             size_t maxPoints) const {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return history.getHistoryChunkJSON(start, maxPoints,
                                     static_cast<int32_t>(timeSync.getUTCOffset()));
}

size_t StateCoordinator::getHistoryCount() const {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return history.getCount();
}

std::string StateCoordinator::serializeHistorySnapshot() const {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return history.serializeSnapshot();
}

bool StateCoordinator::restoreHistorySnapshot(const std::string &snapshotData) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return history.restoreSnapshot(snapshotData);
}

bool StateCoordinator::historyNeedsSnapshot() const {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return history.hasUnsavedChanges();
}

void StateCoordinator::markHistorySnapshotSaved() {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  history.markSnapshotSaved();
}

// =============================================================================
// OBSERVER MANAGEMENT
// =============================================================================

void StateCoordinator::addObserver(IStateObserver *observer) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (observer != nullptr &&
      std::find(observers.begin(), observers.end(), observer) == observers.end()) {
    observers.push_back(observer);
  }
}

void StateCoordinator::removeObserver(IStateObserver *observer) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  observers.erase(std::remove(observers.begin(), observers.end(), observer),
                  observers.end());
}

void StateCoordinator::notifyObservers() {
  std::vector<IStateObserver *> observerSnapshot;
  {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    observerSnapshot = observers;
  }

  for (IStateObserver *observer : observerSnapshot) {
    if (observer != nullptr) {
      observer->onStateChanged();
    }
  }
}
