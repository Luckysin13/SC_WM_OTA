#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include "config/network_config.h"
#include "compat/compat.h"
#include <time.h>

// =============================================================================
// TIME SYNC CLASS
// =============================================================================
// Handles NTP time synchronization with timezone support
// =============================================================================

class TimeSync {
private:
  bool initialized = false;
  char dateBuffer[50];
  char timeBuffer[50];

public:
  // Initialize NTP time synchronization
  // Must be called after WiFi is connected
  // Initialize NTP time synchronization
  // Must be called after WiFi is connected
  bool begin();
  bool begin(const char *tz);

  // Set a new timezone (POSIX TZ string)
  void setTimezone(const char *tz);

  // Check if time has been synchronized
  bool isInitialized() const { return initialized; }

  // Update time strings (call periodically to refresh)
  bool updateTime();

  // Get formatted date string (e.g., "December 28")
  const char *getDateString() const { return dateBuffer; }

  // Get formatted time string (e.g., "14:30PM")
  const char *getTimeString() const { return timeBuffer; }

  // Print current time to Serial
  void printTime();

  // Get current UTC offset in seconds
  long getUTCOffset();
};

#endif // TIME_SYNC_H
