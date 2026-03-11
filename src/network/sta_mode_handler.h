#ifndef STA_MODE_HANDLER_H
#define STA_MODE_HANDLER_H

#include "compat/compat.h"
#include "config/paths_config.h"
#include "network/network_manager.h"
#include "storage/persistent_storage.h"
#include "esp_http_server.h"


// =============================================================================
// STA MODE HANDLER CLASS
// =============================================================================
// Sets up HTTP routes for normal operation (STA mode)
// Serves static files and main control interface
// =============================================================================

class STAModeHandler {
private:
  httpd_handle_t server;
  PersistentStorage &storage;
  NetworkManager &networkMgr;

  static esp_err_t handleRootGet(httpd_req_t *req);
  static esp_err_t handleRootPost(httpd_req_t *req);
  static esp_err_t handleNetworksGet(httpd_req_t *req);
  static esp_err_t handleStatusGet(httpd_req_t *req);
  static esp_err_t handleStatic(httpd_req_t *req);

public:
  STAModeHandler(httpd_handle_t srv, PersistentStorage &stor, NetworkManager &netMgr);
  void setupRoutes();
};

#endif // STA_MODE_HANDLER_H
