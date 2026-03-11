#ifndef PATHS_CONFIG_H
#define PATHS_CONFIG_H

// =============================================================================
// FILE PATHS CONFIGURATION
// =============================================================================
// LittleFS file paths and HTTP parameter names
// =============================================================================

// =============================================================================
// LittleFS FILE PATHS (Credentials)
// =============================================================================
#define PATH_SSID "/ssid.txt"
#define PATH_PASS "/pass.txt"
#define PATH_IP "/ip.txt"
#define PATH_GATEWAY "/gateway.txt"
#define PATH_USE_DHCP "/usedhcp.txt"
#define PATH_PIT_OFFSET "/pit_offset.txt"
#define PATH_MEAT_OFFSET "/meat_offset.txt"
#define PATH_PID_KP "/pid_kp.txt"
#define PATH_PID_KI "/pid_ki.txt"
#define PATH_PID_KD "/pid_kd.txt"
#define PATH_TIMEZONE "/timezone.txt"
#define PATH_MEAT_SETPOINT "/meat_setpoint.txt"
#define PATH_KEEP_WARM_SETPOINT "/kw_setpoint.txt"

// =============================================================================
// HTTP POST PARAMETER NAMES
// =============================================================================
#define PARAM_SSID "ssid"
#define PARAM_PASS "pass"
#define PARAM_IP "ip"
#define PARAM_GATEWAY "gateway"
#define PARAM_ERASE "erase"

// =============================================================================
// WEB PAGE PATHS (LittleFS)
// =============================================================================
#define PATH_INDEX "/index.html"
#define PATH_WIFI "/wifi.html"
#define PATH_REDIRECT "/redirect.html"
#define PATH_ALARMS "/alarms.html"
#define PATH_GRAPH "/graph.html"
#define PATH_CONFIG "/configuration.html"

// =============================================================================
// PWA ASSETS (LittleFS)
// =============================================================================
#define PATH_MANIFEST "/manifest.json"
#define PATH_SW "/sw.js"
#define PATH_OFFLINE "/offline.html"
#define PATH_ICON "/icon-192.svg"

#endif // PATHS_CONFIG_H
