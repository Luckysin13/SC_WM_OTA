#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

// =============================================================================
// NETWORK CONFIGURATION
// =============================================================================
// WiFi, NTP, and network timeout settings
// =============================================================================

// =============================================================================
// ACCESS POINT (AP) MODE CONFIGURATION
// =============================================================================
#define AP_SSID "SMOKER CONTROLLER"
#define AP_PASSWORD "88888888"

// =============================================================================
// WIFI CONNECTION SETTINGS
// =============================================================================
#define WIFI_TIMEOUT_MS 10000      // WiFi connection timeout (milliseconds)

// Default subnet mask for static IP configuration
#define DEFAULT_SUBNET_MASK "255.255.0.0"

// =============================================================================
// NTP TIME SYNCHRONIZATION
// =============================================================================
// Time zone definitions (POSIX format)
#define TZ_EST "EST5EDT,M3.2.0,M11.1.0"     // New York (Eastern)
#define TZ_CST "CST6CDT,M3.2.0,M11.1.0"     // Illinois (Central)
#define TZ_MST "MST7MDT,M3.2.0,M11.1.0"     // Colorado (Mountain)
#define TZ_PST "PST8PDT,M3.2.0,M11.1.0"     // California (Pacific)
#define TZ_AKST "AKST9AKDT,M3.2.0,M11.1.0"  // Alaska
#define TZ_HST "HST10"                       // Hawaii (no DST)
#define TZ_MST_AZ "MST7"                     // Arizona (no DST)

// Current timezone selection
#define CURRENT_TIMEZONE TZ_CST

// NTP server configuration
#define NTP_SERVER_PRIMARY "pool.ntp.org"
#define NTP_SERVER_SECONDARY "time.nist.gov"

#endif // NETWORK_CONFIG_H
