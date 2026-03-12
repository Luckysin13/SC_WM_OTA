#ifndef STATE_COORDINATOR_H
#define STATE_COORDINATOR_H

#include "control/controller_state.h"
#include "control/display_state.h"
#include "control/history_manager.h"
#include "control/sensor_data.h"
#include <mutex>
#include <string>
#include <utility>
#include <vector>


// =============================================================================
// STATE OBSERVER INTERFACE
// =============================================================================
// Observer pattern interface for state change notifications
// =============================================================================

class IStateObserver {
public:
  virtual ~IStateObserver() {}

  // Called when display state changes and UI should be updated
  virtual void onStateChanged() = 0;
};

// =============================================================================
// STATE COORDINATOR CLASS
// =============================================================================
// Central coordinator for system state with dependency injection
// Uses observer pattern for change notifications to WebSocket clients
// =============================================================================

class StateCoordinator {
private:
  SensorData sensors;
  ControllerState controller;
  DisplayState display;
  HistoryManager history;
  mutable std::recursive_mutex mutex;

  // Observer list for WebSocket notifications
  std::vector<IStateObserver *> observers;

public:
  // =========================================================================
  // INITIALIZATION
  // =========================================================================

  // Initialize the coordinator and its components
  void begin();

  // =========================================================================
  // SENSOR DATA ACCESS
  // =========================================================================

  // Get sensor data (const reference for read-only access)
  SensorData getSensors() const;

  // Update sensor data
  void updateSensors(const SensorData &data);

  // =========================================================================
  // CONTROLLER STATE ACCESS
  // =========================================================================

  // Get controller state (const reference for read-only access)
  ControllerState getController() const;

  template <typename Func>
  auto withController(Func &&func)
      -> decltype(func(std::declval<ControllerState &>())) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return func(controller);
  }

  // Update controller state
  void updateController(const ControllerState &state);

  // =========================================================================
  // DISPLAY STATE ACCESS
  // =========================================================================

  // Get display state (const reference for read-only access)
  DisplayState getDisplay() const;

  template <typename Func>
  auto withDisplay(Func &&func)
      -> decltype(func(std::declval<DisplayState &>())) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return func(display);
  }

  template <typename Func>
  auto withState(Func &&func)
      -> decltype(func(std::declval<SensorData &>(),
                       std::declval<ControllerState &>(),
                       std::declval<DisplayState &>(),
                       std::declval<HistoryManager &>())) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return func(sensors, controller, display, history);
  }

  // Update display state and notify observers if changed
  void updateDisplay();

  // =========================================================================
  // HISTORY ACCESS
  // =========================================================================

  // Get history manager contents as JSON for WebSocket retrieval
  String getHistoryJSON() const;
  String getHistoryChunkJSON(size_t start, size_t maxPoints) const;
  size_t getHistoryCount() const;
  std::string serializeHistorySnapshot() const;
  bool restoreHistorySnapshot(const std::string &snapshotData);
  bool historyNeedsSnapshot() const;
  void markHistorySnapshotSaved();

  // =========================================================================
  // OBSERVER MANAGEMENT
  // =========================================================================

  // Register an observer for state change notifications
  void addObserver(IStateObserver *observer);

  // Remove an observer
  void removeObserver(IStateObserver *observer);

  // Notify all observers of state change
  void notifyObservers();
};

#endif // STATE_COORDINATOR_H
