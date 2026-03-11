#ifndef STATE_COORDINATOR_H
#define STATE_COORDINATOR_H

#include "control/controller_state.h"
#include "control/display_state.h"
#include "control/history_manager.h"
#include "control/sensor_data.h"
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
  const SensorData &getSensors() const;

  // Update sensor data
  void updateSensors(const SensorData &data);

  // =========================================================================
  // CONTROLLER STATE ACCESS
  // =========================================================================

  // Get controller state (const reference for read-only access)
  const ControllerState &getController() const;

  // Get controller state (mutable reference for PID computation)
  ControllerState &getControllerMutable();

  // Update controller state
  void updateController(const ControllerState &state);

  // =========================================================================
  // DISPLAY STATE ACCESS
  // =========================================================================

  // Get display state (const reference for read-only access)
  const DisplayState &getDisplay() const;

  // Get display state (mutable reference for updates)
  DisplayState &getDisplayMutable();

  // Update display state and notify observers if changed
  void updateDisplay();

  // =========================================================================
  // HISTORY ACCESS
  // =========================================================================

  // Get history manager (mutable for updates/retrieval)
  HistoryManager &getHistory();

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
