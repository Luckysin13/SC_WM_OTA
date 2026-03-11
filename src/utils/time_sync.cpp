#include "time_sync.h"
#include "esp_sntp.h"

// =============================================================================
// TIME SYNC IMPLEMENTATION
// =============================================================================

bool TimeSync::begin() { return begin(CURRENT_TIMEZONE); }

bool TimeSync::begin(const char *tz) {
  Serial.println("\n[INFO] Initializing NTP time sync...");
  Serial.printf("  Target Timezone: %s\n", tz);

  // Configure and start SNTP
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, NTP_SERVER_PRIMARY);
  sntp_setservername(1, NTP_SERVER_SECONDARY);
  sntp_init();

  // Set timezone environment variable
  setTimezone(tz);

  Serial.printf("  NTP Server 1: %s\n", NTP_SERVER_PRIMARY);
  Serial.printf("  NTP Server 2: %s\n", NTP_SERVER_SECONDARY);

  // Wait a moment for time to sync
  // We check periodically instead of one long delay
  int retry = 0;
  while (retry < 10) {
    time_t now;
    time(&now);
    if (now > 100000) { // Check if we have a valid timestamp (> 1970)
      break;
    }
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println();

  // Try to get time to verify sync
  time_t now;
  time(&now);
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    Serial.println("[OK] NTP time synchronized");
    initialized = true;
    updateTime(); // Update buffers with initial time
    printTime();  // Log the initial time
    return true;
  } else {
    Serial.println("[WARN] NTP time sync pending...");
    initialized = false;
    return false;
  }
}

void TimeSync::setTimezone(const char *tz) {
  Serial.printf("[INFO] Setting Timezone to: %s\n", tz);
  setenv("TZ", tz, 1);
  tzset();

  // Debug: Check offset immediately
  long offset = getUTCOffset();
  Serial.printf("       Current UTC Offset: %ld seconds (%.1f hours)\n", offset,
                offset / 3600.0);

  updateTime(); // Refresh buffers immediately
}

bool TimeSync::updateTime() {
  time_t now;
  time(&now);
  struct tm timeinfo;
  if (!localtime_r(&now, &timeinfo)) {
    return false;
  }

  // Format date (e.g., "December 28")
  strftime(dateBuffer, sizeof(dateBuffer), "%B %d", &timeinfo);

  // Format time (e.g., "02:30 PM")
  strftime(timeBuffer, sizeof(timeBuffer), "%I:%M %p", &timeinfo);

  return true;
}

void TimeSync::printTime() {
  if (updateTime()) {
    Serial.printf("Date: %s\n", dateBuffer);
    Serial.printf("Time: %s\n", timeBuffer);
  } else {
    Serial.println("Failed to obtain time");
  }
}

long TimeSync::getUTCOffset() {
  time_t now = time(nullptr);
  struct tm tm_utc;
  // Get broken-down UTC time
  gmtime_r(&now, &tm_utc);

  // Important: set isdst to -1 to let mktime determine it based on TZ
  // Or set to 0 if we assume standard time?
  // mktime interprets the input struct AS LOCAL TIME.
  // If we feed it the UTC struct, it calculates the timestamp "as if" that UTC
  // time was Local. Example: Real UTC 17:00. tm_utc = 17:00. TZ = EST (-5).
  // mktime(17:00) treats it as 17:00 EST.
  // 17:00 EST is 22:00 UTC.
  // So mktime returns timestamp for 22:00 UTC.
  // Difference: 17:00 - 22:00 = -5 hours.

  tm_utc.tm_isdst = -1;
  time_t t_local_as_utc = mktime(&tm_utc);

  long offset = (long)difftime(now, t_local_as_utc);

  // Debug logic - print only if queried or changed?
  // Since this is called frequently for JSON, we should avoid serial spam
  // unless needed. But for now, user needs to debug. static long lastOffset =
  // 0; if (offset != lastOffset) {
  //   Serial.printf("[Time] New Offset Calculated: %ld (%.1f hrs)\n", offset,
  //   offset/3600.0); lastOffset = offset;
  // }

  return offset;
}
