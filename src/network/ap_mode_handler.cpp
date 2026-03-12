#include "ap_mode_handler.h"
#include "config/paths_config.h"
#include "esp_system.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

// =============================================================================
// AP MODE HANDLER IMPLEMENTATION (ESP-IDF)
// =============================================================================

APModeHandler::APModeHandler(httpd_handle_t srv, PersistentStorage &stor,
                             NetworkManager &netMgr)
    : server(srv), storage(stor), networkMgr(netMgr) {}

void APModeHandler::setupRoutes() {
  Serial.println("\n[INFO] Setting up AP mode web routes...");

  httpd_uri_t root_get = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleRootGet,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &root_get);

  httpd_uri_t root_post = {
      .uri = "/",
      .method = HTTP_POST,
      .handler = &APModeHandler::handleRootPost,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &root_post);

  httpd_uri_t config_get = {
      .uri = "/configuration.html",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleStatic,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &config_get);

  httpd_uri_t networks_get = {
      .uri = "/api/networks",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleNetworksGet,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &networks_get);

  httpd_uri_t status_get = {
      .uri = "/api/status",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleStatusGet,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &status_get);

    httpd_uri_t captive_generate_204 = {
      .uri = "/generate_204",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleCaptivePortal,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
    };
    httpd_register_uri_handler(server, &captive_generate_204);

    httpd_uri_t captive_hotspot_detect = {
      .uri = "/hotspot-detect.html",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleCaptivePortal,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
    };
    httpd_register_uri_handler(server, &captive_hotspot_detect);

    httpd_uri_t captive_ncsi = {
      .uri = "/ncsi.txt",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleCaptivePortal,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
    };
    httpd_register_uri_handler(server, &captive_ncsi);

    httpd_uri_t captive_connecttest = {
      .uri = "/connecttest.txt",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleCaptivePortal,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
    };
    httpd_register_uri_handler(server, &captive_connecttest);

    httpd_uri_t captive_success = {
      .uri = "/success.txt",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleCaptivePortal,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
    };
    httpd_register_uri_handler(server, &captive_success);

    httpd_uri_t captive_canonical = {
      .uri = "/canonical.html",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleCaptivePortal,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
    };
    httpd_register_uri_handler(server, &captive_canonical);

  httpd_uri_t static_get = {
      .uri = "/*",
      .method = HTTP_GET,
      .handler = &APModeHandler::handleStatic,
      .user_ctx = this,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
  };
  httpd_register_uri_handler(server, &static_get);

  Serial.println("[OK] AP mode routes configured");
}

namespace {
constexpr const char *kFsBase = "/littlefs";

// Cache durations
constexpr const char *CACHE_STATIC = "public, max-age=86400";  // 24 hours for CSS, JS
constexpr const char *CACHE_HTML = "no-cache, must-revalidate"; // Always revalidate HTML

bool readRequestBody(httpd_req_t *req, std::string &body) {
  body.clear();
  body.resize(req->content_len);

  int offset = 0;
  while (offset < req->content_len) {
    int received = httpd_req_recv(req, body.data() + offset, req->content_len - offset);
    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (received <= 0) {
      body.clear();
      return false;
    }
    offset += received;
  }

  return true;
}

int fromHexDigit(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  return -1;
}

String decodeFormComponent(const char *value) {
  std::string decoded;
  decoded.reserve(std::strlen(value));

  for (size_t index = 0; value[index] != '\0'; ++index) {
    char ch = value[index];
    if (ch == '+') {
      decoded.push_back(' ');
      continue;
    }
    if (ch == '%' && value[index + 1] != '\0' && value[index + 2] != '\0') {
      int high = fromHexDigit(value[index + 1]);
      int low = fromHexDigit(value[index + 2]);
      if (high >= 0 && low >= 0) {
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
        continue;
      }
    }
    decoded.push_back(ch);
  }

  return String(decoded);
}

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

esp_err_t sendRedirect(httpd_req_t *req, const char *location) {
  if (!location) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", location);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, "<html><body>Redirecting...</body></html>", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}
} // namespace

extern void requestSystemRestart(unsigned long delayMs);

esp_err_t APModeHandler::handleRootGet(httpd_req_t *req) {
  return sendFile(req, PATH_WIFI);
}

esp_err_t APModeHandler::handleRootPost(httpd_req_t *req) {
  auto *self = static_cast<APModeHandler *>(req->user_ctx);

  std::string body;
  if (!readRequestBody(req, body)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    return ESP_FAIL;
  }

  auto getParam = [&](const char *key) -> String {
    char value[128];
    if (httpd_query_key_value(body.c_str(), key, value, sizeof(value)) == ESP_OK) {
      return decodeFormComponent(value);
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
    requestSystemRestart(3000);
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

  String page = self->buildRedirectPage(newCreds);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, page.c_str(), page.length());

  requestSystemRestart(2000);
  return ESP_OK;
}

esp_err_t APModeHandler::handleNetworksGet(httpd_req_t *req) {
  auto *self = static_cast<APModeHandler *>(req->user_ctx);
  String json = self->getNetworkListJSON();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), json.length());
  return ESP_OK;
}

esp_err_t APModeHandler::handleStatusGet(httpd_req_t *req) {
  auto *self = static_cast<APModeHandler *>(req->user_ctx);
  String json = "{";
  json += "\"mode\":\"AP\",";
  json += "\"connected\":true,";
  json += "\"ssid\":\"" + String(AP_SSID) + "\",";
  json += "\"ip\":\"" + self->networkMgr.getLocalIP().toString() + "\"";
  json += "}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), json.length());
  return ESP_OK;
}

esp_err_t APModeHandler::handleCaptivePortal(httpd_req_t *req) {
  return sendRedirect(req, PATH_WIFI);
}

esp_err_t APModeHandler::handleStatic(httpd_req_t *req) {
  std::string path = req->uri;
  size_t queryPos = path.find('?');
  if (queryPos != std::string::npos) {
    path = path.substr(0, queryPos);
  }
  if (path == "/" || path.empty()) {
    path = PATH_WIFI;
  }
  return sendFile(req, path.c_str());
}

String APModeHandler::getNetworkListJSON() {
  int numNetworks = networkMgr.scanNetworks();
  String json = "{\"networks\":[";

  if (numNetworks > 0) {
    for (int i = 0; i < numNetworks; i++) {
      if (i > 0)
        json += ",";
      json += "{";
      json += "\"ssid\":\"" + networkMgr.getScannedSSID(i) + "\",";
      json += "\"rssi\":" + String(networkMgr.getScannedRSSI(i)) + ",";
      json += "\"secure\":" +
              String(networkMgr.isScannedNetworkOpen(i) ? "false" : "true");
      json += "}";
    }
  }

  json += "]}";
  return json;
}

String APModeHandler::buildRedirectPage(const WiFiCredentials &creds) {
  std::string content = readFileToString(PATH_REDIRECT);
  if (content.empty()) {
    return String("Configuration saved. Restarting...");
  }

  replaceAll(content, "%SSID%", creds.ssid.c_str());
  replaceAll(content, "%IP_MODE%", creds.useDHCP ? "DHCP (Auto)" : "Static");
  replaceAll(content, "%IP_ADDRESS%", creds.useDHCP ? "Assigned by Router" : creds.ip.c_str());
  replaceAll(content, "%GATEWAY%", creds.useDHCP ? "Assigned by Router" : creds.gateway.c_str());

  if (creds.useDHCP) {
    replaceAll(content, "%IP_MSG%", "<p>Router will assign IP via DHCP.</p>");
  } else {
    std::string link = "<div style='margin-top: 20px; padding: 15px; background: "
                       "rgba(6, 182, 212, 0.1); border-radius: 8px; border: 1px "
                       "solid var(--accent-cyan);'>";
    link += "<p style='margin: 0 0 10px 0; color: var(--text-secondary);'>Redirecting to:</p>";
    link += "<a href='https://" + creds.ip.str() +
            "' class='redirect-link' style='font-size: 24px; color: var(--accent-cyan); text-decoration: underline; font-weight: bold; word-break: break-all;'>https://" +
            creds.ip.str() + "</a>";
    link += "<p style='margin: 10px 0 0 0; font-size: 12px; color: var(--text-muted);'>Click the link if not redirected automatically.</p>";
    link += "</div>";
    replaceAll(content, "%IP_MSG%", link);
  }

  return String(content);
}

std::string APModeHandler::readFileToString(const char *path) {
  std::string fullPath = std::string("/littlefs") + (path[0] == '/' ? "" : "/") + path;
  FILE *file = fopen(fullPath.c_str(), "r");
  if (!file) {
    return {};
  }
  std::string result;
  char buffer[256];
  size_t read = 0;
  while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    result.append(buffer, read);
  }
  fclose(file);
  return result;
}

void APModeHandler::replaceAll(std::string &input, const std::string &from, const std::string &to) {
  if (from.empty()) {
    return;
  }
  size_t start = 0;
  while ((start = input.find(from, start)) != std::string::npos) {
    input.replace(start, from.length(), to);
    start += to.length();
  }
}
