#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

// =============================================================================
// DEBUG CONFIGURATION
// =============================================================================
// Professional debug output control for the Smoker Controller.
// This header provides compile-time and runtime control over serial debug output.
//
// USAGE:
//   - Set DEBUG_ENABLED to 1 to enable all debug output (development builds)
//   - Set DEBUG_ENABLED to 0 to disable all debug output (production builds)
//   - Use DEBUG_LEVEL to control verbosity when debugging is enabled
//
// MACROS:
//   DBG_PRINT(msg)        - Print without newline (if DEBUG_ENABLED)
//   DBG_PRINTLN(msg)      - Print with newline (if DEBUG_ENABLED)
//   DBG_PRINTF(fmt, ...)  - Formatted print (if DEBUG_ENABLED)
//   
//   DBG_ERROR(fmt, ...)   - Error messages (Level 1+) - Critical failures
//   DBG_WARN(fmt, ...)    - Warning messages (Level 2+) - Potential issues
//   DBG_INFO(fmt, ...)    - Info messages (Level 3+) - General operation info
//   DBG_DEBUG(fmt, ...)   - Debug messages (Level 4+) - Detailed debugging
//   DBG_VERBOSE(fmt, ...) - Verbose messages (Level 5) - Very detailed output
// =============================================================================

// =============================================================================
// MASTER DEBUG SWITCH
// =============================================================================
// Set to 1 to enable debug output, 0 to disable completely
// In production builds, set this to 0 to eliminate all Serial overhead
#ifndef DEBUG_ENABLED
#define DEBUG_ENABLED 1
#endif

// =============================================================================
// DEBUG VERBOSITY LEVELS
// =============================================================================
// Higher levels include all lower level messages
//   0 = Off (no output even if DEBUG_ENABLED is 1)
//   1 = Errors only
//   2 = Errors + Warnings
//   3 = Errors + Warnings + Info (recommended for normal operation)
//   4 = All above + Debug (for development)
//   5 = All above + Verbose (very detailed, use sparingly)
#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 3
#endif

// =============================================================================
// MODULE-SPECIFIC DEBUG FLAGS
// =============================================================================
// Enable/disable debug output for specific modules independently
// These only apply when DEBUG_ENABLED is 1

#ifndef DEBUG_NETWORK
#define DEBUG_NETWORK 1
#endif

#ifndef DEBUG_WEBSOCKET
#define DEBUG_WEBSOCKET 1
#endif

#ifndef DEBUG_OTA
#define DEBUG_OTA 1
#endif

#ifndef DEBUG_TEMPERATURE
#define DEBUG_TEMPERATURE 1
#endif

#ifndef DEBUG_PID
#define DEBUG_PID 1
#endif

#ifndef DEBUG_STORAGE
#define DEBUG_STORAGE 1
#endif

#ifndef DEBUG_TIME
#define DEBUG_TIME 1
#endif

// =============================================================================
// DEBUG MACRO IMPLEMENTATIONS
// =============================================================================

#if DEBUG_ENABLED

// Basic debug output (respects DEBUG_ENABLED only)
#define DBG_PRINT(msg)        Serial.print(msg)
#define DBG_PRINTLN(msg)      Serial.println(msg)
#define DBG_PRINTF(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)

// Level-based debug output
#if DEBUG_LEVEL >= 1
#define DBG_ERROR(fmt, ...)   Serial.printf("[ERROR] " fmt, ##__VA_ARGS__)
#else
#define DBG_ERROR(fmt, ...)   ((void)0)
#endif

#if DEBUG_LEVEL >= 2
#define DBG_WARN(fmt, ...)    Serial.printf("[WARN] " fmt, ##__VA_ARGS__)
#else
#define DBG_WARN(fmt, ...)    ((void)0)
#endif

#if DEBUG_LEVEL >= 3
#define DBG_INFO(fmt, ...)    Serial.printf("[INFO] " fmt, ##__VA_ARGS__)
#else
#define DBG_INFO(fmt, ...)    ((void)0)
#endif

#if DEBUG_LEVEL >= 4
#define DBG_DEBUG(fmt, ...)   Serial.printf("[DEBUG] " fmt, ##__VA_ARGS__)
#else
#define DBG_DEBUG(fmt, ...)   ((void)0)
#endif

#if DEBUG_LEVEL >= 5
#define DBG_VERBOSE(fmt, ...) Serial.printf("[VERBOSE] " fmt, ##__VA_ARGS__)
#else
#define DBG_VERBOSE(fmt, ...) ((void)0)
#endif

// Module-specific debug macros
#if DEBUG_NETWORK
#define DBG_NET(fmt, ...)     Serial.printf("[NET] " fmt, ##__VA_ARGS__)
#else
#define DBG_NET(fmt, ...)     ((void)0)
#endif

#if DEBUG_WEBSOCKET
#define DBG_WS(fmt, ...)      Serial.printf("[WS] " fmt, ##__VA_ARGS__)
#else
#define DBG_WS(fmt, ...)      ((void)0)
#endif

#if DEBUG_OTA
#define DBG_OTA(fmt, ...)     Serial.printf("[OTA] " fmt, ##__VA_ARGS__)
#else
#define DBG_OTA(fmt, ...)     ((void)0)
#endif

#if DEBUG_TEMPERATURE
#define DBG_TEMP(fmt, ...)    Serial.printf("[TEMP] " fmt, ##__VA_ARGS__)
#else
#define DBG_TEMP(fmt, ...)    ((void)0)
#endif

#if DEBUG_PID
#define DBG_PID(fmt, ...)     Serial.printf("[PID] " fmt, ##__VA_ARGS__)
#else
#define DBG_PID(fmt, ...)     ((void)0)
#endif

#if DEBUG_STORAGE
#define DBG_STOR(fmt, ...)    Serial.printf("[STOR] " fmt, ##__VA_ARGS__)
#else
#define DBG_STOR(fmt, ...)    ((void)0)
#endif

#if DEBUG_TIME
#define DBG_TIME(fmt, ...)    Serial.printf("[TIME] " fmt, ##__VA_ARGS__)
#else
#define DBG_TIME(fmt, ...)    ((void)0)
#endif

#else // DEBUG_ENABLED == 0

// When debug is disabled, all macros expand to nothing
#define DBG_PRINT(msg)        ((void)0)
#define DBG_PRINTLN(msg)      ((void)0)
#define DBG_PRINTF(fmt, ...)  ((void)0)

#define DBG_ERROR(fmt, ...)   ((void)0)
#define DBG_WARN(fmt, ...)    ((void)0)
#define DBG_INFO(fmt, ...)    ((void)0)
#define DBG_DEBUG(fmt, ...)   ((void)0)
#define DBG_VERBOSE(fmt, ...) ((void)0)

#define DBG_NET(fmt, ...)     ((void)0)
#define DBG_WS(fmt, ...)      ((void)0)
#define DBG_OTA(fmt, ...)     ((void)0)
#define DBG_TEMP(fmt, ...)    ((void)0)
#define DBG_PID(fmt, ...)     ((void)0)
#define DBG_STOR(fmt, ...)    ((void)0)
#define DBG_TIME(fmt, ...)    ((void)0)

#endif // DEBUG_ENABLED

// =============================================================================
// UTILITY MACROS
// =============================================================================

// Conditional compilation helper - use in code blocks
#if DEBUG_ENABLED
#define IF_DEBUG(code) code
#else
#define IF_DEBUG(code)
#endif

// Print separator lines for visual organization
#define DBG_SEPARATOR() DBG_PRINTLN("-------------------------------------------")
#define DBG_HEADER()    DBG_PRINTLN("===========================================")

// Print variable name and value (useful for quick debugging)
#define DBG_VAR(var)    DBG_PRINTF(#var " = %d\n", (int)(var))
#define DBG_VARF(var)   DBG_PRINTF(#var " = %.2f\n", (float)(var))
#define DBG_VARS(var)   DBG_PRINTF(#var " = %s\n", (var))

#endif // DEBUG_CONFIG_H
