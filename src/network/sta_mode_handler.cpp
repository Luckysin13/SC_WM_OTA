#include "sta_mode_handler.h"
#include "config/paths_config.h"
#include <string>

namespace {
constexpr const char *kFsBase = "/littlefs";

// Cache durations
constexpr const char *CACHE_STATIC = "public, max-age=86400";  // 24 hours for CSS, JS
constexpr const char *CACHE_HTML = "no-cache, must-revalidate"; // Always revalidate HTML

bool setContentType(httpd_req_t *req, const std::string &path) {
  if (path.find(".css") != std::string::npos) {
    return httpd_resp_set_type(req, "text/css") == ESP_OK;
  }
  if (path.find(".json") != std::string::npos) {
    return httpd_resp_set_type(req, "application/json") == ESP_OK;
  }
  if (path.find(".js") != std::string::npos) {
    return httpd_resp_set_type(req, "application/javascript") == ESP_OK;
  }
  if (path.find(".html") != std::string::npos) {
    return httpd_resp_set_type(req, "text/html") == ESP_OK;
  }
  if (path.find(".png") != std::string::npos) {
    return httpd_resp_set_type(req, "image/png") == ESP_OK;
  }
  if (path.find(".ico") != std::string::npos) {
    return httpd_resp_set_type(req, "image/x-icon") == ESP_OK;
  }
  if (path.find(".svg") != std::string::npos) {
    return httpd_resp_set_type(req, "image/svg+xml") == ESP_OK;
  }
  return true;
}

// Check if client accepts gzip encoding
bool clientAcceptsGzip(httpd_req_t *req) {
  char buf[128];
  if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", buf, sizeof(buf)) == ESP_OK) {
    return strstr(buf, "gzip") != nullptr;
  }
  return false;
}

// Set cache control headers based on file type
void setCacheHeaders(httpd_req_t *req, const std::string &path) {
  if (path.find(".html") != std::string::npos) {
    httpd_resp_set_hdr(req, "Cache-Control", CACHE_HTML);
  } else if (path.find(".css") != std::string::npos || 
             path.find(".png") != std::string::npos ||
             path.find(".svg") != std::string::npos ||
             path.find(".ico") != std::string::npos) {
    httpd_resp_set_hdr(req, "Cache-Control", CACHE_STATIC);
  } else if (path.find("sw.js") != std::string::npos) {
    // Service worker needs special headers
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Service-Worker-Allowed", "/");
  } else if (path.find(".js") != std::string::npos) {
    httpd_resp_set_hdr(req, "Cache-Control", CACHE_STATIC);
  } else if (path.find("manifest.json") != std::string::npos) {
    httpd_resp_set_hdr(req, "Cache-Control", CACHE_HTML);
  }
}

esp_err_t sendFile(httpd_req_t *req, const char *path) {
  if (!path) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  std::string cleanPath = path;
  size_t queryPos = cleanPath.find('?');
  if (queryPos != std::string::npos) {
    cleanPath = cleanPath.substr(0, queryPos);
  }
  if (cleanPath.empty()) {
    cleanPath = "/";
  }

  std::string basePath = std::string(kFsBase) + (cleanPath[0] == '/' ? "" : "/") + cleanPath;
  std::string fullPath = basePath;
  bool useGzip = false;

  std::string gzPath = basePath + ".gz";
  FILE *file = nullptr;

  // Prefer gzip when supported, but always fall back to .gz if plain file is missing.
  if (clientAcceptsGzip(req)) {
    file = fopen(gzPath.c_str(), "r");
    if (file) {
      fullPath = gzPath;
      useGzip = true;
    }
  }

  if (!file) {
    file = fopen(fullPath.c_str(), "r");
  }

  if (!file) {
    file = fopen(gzPath.c_str(), "r");
    if (file) {
      fullPath = gzPath;
      useGzip = true;
    }
  }

  if (!file) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  // Set content type based on original path (not .gz)
  if (!setContentType(req, basePath)) {
    fclose(file);
    return ESP_FAIL;
  }

  // Set gzip encoding header if serving compressed file
  if (useGzip) {
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  }

  // Set cache headers
  setCacheHeaders(req, basePath);

  // Use larger buffer for faster transfers
  char buffer[2048];
  size_t read = 0;
  while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    if (httpd_resp_send_chunk(req, buffer, read) != ESP_OK) {
      fclose(file);
      httpd_resp_sendstr_chunk(req, nullptr);
      return ESP_FAIL;
    }
  }
  fclose(file);
  httpd_resp_sendstr_chunk(req, nullptr);
  return ESP_OK;
}
} // namespace

// =============================================================================
// STA MODE HANDLER IMPLEMENTATION (ESP-IDF)
// =============================================================================

STAModeHandler::STAModeHandler(httpd_handle_t srv, PersistentStorage &stor,
                               NetworkManager &netMgr)
    : server(srv), storage(stor), networkMgr(netMgr) {}

void STAModeHandler::setupRoutes() {
  Serial.println("\n[INFO] Setting up STA mode web routes...");

  httpd_uri_t root_get = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = &STAModeHandler::handleRootGet,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &root_get);

  httpd_uri_t root_post = {
      .uri = "/",
      .method = HTTP_POST,
      .handler = &STAModeHandler::handleRootPost,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &root_post);

  httpd_uri_t config_get = {
      .uri = "/configuration.html",
      .method = HTTP_GET,
      .handler = &STAModeHandler::handleStatic,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &config_get);

  httpd_uri_t networks_get = {
      .uri = "/api/networks",
      .method = HTTP_GET,
      .handler = &STAModeHandler::handleNetworksGet,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &networks_get);

  httpd_uri_t status_get = {
      .uri = "/api/status",
      .method = HTTP_GET,
      .handler = &STAModeHandler::handleStatusGet,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &status_get);

  httpd_uri_t static_get = {
      .uri = "/*",
      .method = HTTP_GET,
      .handler = &STAModeHandler::handleStatic,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &static_get);

  Serial.println("[OK] STA mode routes configured");
}

esp_err_t STAModeHandler::handleRootGet(httpd_req_t *req) {
  return sendFile(req, PATH_INDEX);
}

esp_err_t STAModeHandler::handleRootPost(httpd_req_t *req) {
  auto *self = static_cast<STAModeHandler *>(req->user_ctx);

  int total = req->content_len;
  std::string body;
  body.resize(total);
  int received = httpd_req_recv(req, body.data(), total);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    return ESP_FAIL;
  }
  body.resize(received);

  auto getParam = [&](const char *key) -> String {
    char value[128];
    if (httpd_query_key_value(body.c_str(), key, value, sizeof(value)) == ESP_OK) {
      return String(value);
    }
    return String();
  };

  WiFiCredentials newCreds;
  String eraseValue = getParam(PARAM_ERASE);
  bool eraseRequested = (eraseValue == "true" || eraseValue == "on" || eraseValue == "1");

  if (!eraseRequested) {
    newCreds.ssid = getParam(PARAM_SSID);
    newCreds.password = getParam(PARAM_PASS);
    newCreds.ip = getParam(PARAM_IP);
    newCreds.gateway = getParam(PARAM_GATEWAY);
    String dhcpVal = getParam("usedhcp");
    newCreds.useDHCP = (dhcpVal == "true" || dhcpVal == "on" || dhcpVal == "1");
  }

  if (eraseRequested) {
    self->storage.listAllFiles();
    self->storage.eraseCredentials();
    self->storage.listAllFiles();
    httpd_resp_send(req, "Credentials erased. Restarting...", HTTPD_RESP_USE_STRLEN);
    delay(3000);
    ESP::restart();
    return ESP_OK;
  }

  if (newCreds.ssid.isEmpty()) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid configuration. SSID is required.");
    return ESP_FAIL;
  }

  if (!newCreds.useDHCP && newCreds.ip.isEmpty()) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "Invalid configuration. IP address required for static mode.");
    return ESP_FAIL;
  }

  self->storage.saveCredentials(newCreds);
  String message = "Configuration saved. Restarting...\n";
  if (newCreds.useDHCP) {
    message += "Router will assign IP via DHCP.";
  } else {
    message += String("Connect to IP: ") + newCreds.ip;
  }
  httpd_resp_send(req, message.c_str(), message.length());
  delay(2000);
  ESP::restart();
  return ESP_OK;
}

esp_err_t STAModeHandler::handleNetworksGet(httpd_req_t *req) {
  auto *self = static_cast<STAModeHandler *>(req->user_ctx);
  String json = "{\"networks\":[]}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), json.length());
  return ESP_OK;
}

esp_err_t STAModeHandler::handleStatusGet(httpd_req_t *req) {
  auto *self = static_cast<STAModeHandler *>(req->user_ctx);
  const bool connected = self->networkMgr.isConnected();
  String json = "{";
  json += "\"mode\":\"STA\",";
  json += "\"connected\":" + String(connected ? "true" : "false") + ",";
  json += "\"ssid\":\"" + (connected ? self->networkMgr.getSSID() : String("")) + "\",";
  json += "\"rssi\":" + String(connected ? self->networkMgr.getRSSI() : 0) + ",";
  json += "\"ip\":\"" + self->networkMgr.getLocalIP().toString() + "\"";
  json += "}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), json.length());
  return ESP_OK;
}

esp_err_t STAModeHandler::handleStatic(httpd_req_t *req) {
  std::string path = req->uri;
  size_t queryPos = path.find('?');
  if (queryPos != std::string::npos) {
    path = path.substr(0, queryPos);
  }
  if (path == "/" || path.empty()) {
    path = PATH_INDEX;
  }
  return sendFile(req, path.c_str());
}
