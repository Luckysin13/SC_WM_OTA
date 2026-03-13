#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "compat/compat.h"
#include "esp_wifi.h"
#include <vector>
#include "config/network_config.h"
#include "storage/persistent_storage.h"

// =============================================================================
// NETWORK MANAGER CLASS
// =============================================================================
// Handles WiFi connectivity in both STA and AP modes
// Automatically falls back to AP mode if STA connection fails
// =============================================================================

enum class NetworkMode {
    NONE,
    STA,    // Station mode (connect to existing network)
    AP      // Access Point mode (create configuration network)
};

class NetworkManager {
private:
    NetworkMode currentMode = NetworkMode::NONE;
    WiFiCredentials credentials;
    IPAddress localIP;
    IPAddress localGateway;
    IPAddress subnet;
    std::vector<wifi_ap_record_t> scannedNetworks;

    // Try to connect to WiFi in STA mode
    bool connectSTA();

    // Start Access Point mode for configuration
    bool startAP();

    // Start fallback AP while connected in STA mode
    bool startFallbackAP();

public:
    // Initialize network with stored credentials
    // Tries STA mode first, falls back to AP mode if no credentials or connection fails
    // Returns current mode after initialization
    NetworkMode begin(const WiFiCredentials& creds);

    // Get current network mode
    NetworkMode getMode() const { return currentMode; }

    // Check if connected to WiFi
    bool isConnected() const;

    // Check if AP mode is currently active (AP only or AP+STA fallback)
    bool isAccessPointActive() const;

    // Get local IP address
    IPAddress getLocalIP() const;

    // Get SSID of connected network
    String getSSID() const;

    // Get RSSI of connected network
    int getRSSI() const;

    // Scan for available networks
    int scanNetworks();

    // Get scanned network info
    String getScannedSSID(int index);
    int getScannedRSSI(int index);
    bool isScannedNetworkOpen(int index);
};

#endif // NETWORK_MANAGER_H
