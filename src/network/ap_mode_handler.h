#ifndef AP_MODE_HANDLER_H
#define AP_MODE_HANDLER_H

#include "compat/compat.h"
#include "config/paths_config.h"
#include "config/network_config.h"
#include "network/network_manager.h"
#include "storage/persistent_storage.h"
#include "esp_http_server.h"
#include <string>

// =============================================================================
// AP MODE HANDLER CLASS
// =============================================================================
// Sets up HTTP routes for Access Point configuration mode
// Allows initial WiFi setup through captive portal
// =============================================================================

class APModeHandler {
private:
    httpd_handle_t server;
    PersistentStorage& storage;
    NetworkManager& networkMgr;

    static esp_err_t handleRootGet(httpd_req_t* req);
    static esp_err_t handleRootPost(httpd_req_t* req);
    static esp_err_t handleNetworksGet(httpd_req_t* req);
    static esp_err_t handleStatusGet(httpd_req_t* req);
    static esp_err_t handleCaptivePortal(httpd_req_t* req);
    static esp_err_t handleStatic(httpd_req_t* req);

    String buildRedirectPage(const WiFiCredentials& creds);
    static std::string readFileToString(const char* path);
    static void replaceAll(std::string& input, const std::string& from, const std::string& to);

public:
    // Constructor takes references to server and storage
    APModeHandler(httpd_handle_t srv, PersistentStorage& stor, NetworkManager& netMgr);

    // Setup all AP mode routes
    void setupRoutes();

    // Scan and list available WiFi networks (HTML format)
    // Scan and list available WiFi networks (JSON format)
    String getNetworkListJSON();
};

#endif // AP_MODE_HANDLER_H
