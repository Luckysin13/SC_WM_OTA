#include "network_manager.h"
#include "compat/compat.h"
#include "config/network_config.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include <cstring>

// =============================================================================
// NETWORK MANAGER IMPLEMENTATION (ESP-IDF)
// =============================================================================

namespace {
constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kFailBit = BIT1;

EventGroupHandle_t s_wifiEventGroup = nullptr;
esp_netif_t *s_staNetif = nullptr;
esp_netif_t *s_apNetif = nullptr;
bool s_initialized = false;
bool s_mdnsInitialized = false;
int s_lastDisconnectReason = 0;
volatile bool s_staConnected = false;
volatile bool s_apActive = false;

void updateMdnsForNetif(esp_netif_t *netif, mdns_event_actions_t actions,
                        const char *label) {
  if (!s_mdnsInitialized || netif == nullptr) {
    return;
  }

  esp_err_t err = mdns_netif_action(netif, actions);
  if (err != ESP_OK) {
    Serial.printf("[WARN] mDNS netif action failed for %s: %s\n", label,
                  esp_err_to_name(err));
  }
}

const char *wifiDisconnectReasonToString(int reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
      return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE:
      return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_EXPIRE:
      return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_TOOMANY:
      return "ASSOC_TOOMANY";
    case WIFI_REASON_NOT_AUTHED:
      return "NOT_AUTHED";
    case WIFI_REASON_NOT_ASSOCED:
      return "NOT_ASSOCED";
    case WIFI_REASON_ASSOC_LEAVE:
      return "ASSOC_LEAVE";
    case WIFI_REASON_ASSOC_NOT_AUTHED:
      return "ASSOC_NOT_AUTHED";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD:
      return "DISASSOC_PWRCAP_BAD";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
      return "DISASSOC_SUPCHAN_BAD";
    case WIFI_REASON_NO_AP_FOUND:
      return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL:
      return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL:
      return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL:
      return "CONNECTION_FAIL";
    case WIFI_REASON_BEACON_TIMEOUT:
      return "BEACON_TIMEOUT";
    default:
      return "UNKNOWN";
  }
}

void wifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id,
                      void *event_data) {
  (void)arg;

  if (event_base == WIFI_EVENT) {
    if (event_id == WIFI_EVENT_STA_START) {
      s_staConnected = false;
      Serial.println("[WIFI] STA start");
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
      Serial.println("[WIFI] STA connected");
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
      s_staConnected = false;
      auto *disconnected = static_cast<wifi_event_sta_disconnected_t *>(event_data);
      if (disconnected) {
        s_lastDisconnectReason = disconnected->reason;
        Serial.printf("[WIFI] STA disconnected. reason=%d (%s)\n",
                      s_lastDisconnectReason,
                      wifiDisconnectReasonToString(s_lastDisconnectReason));
      } else {
        s_lastDisconnectReason = 0;
        Serial.println("[WIFI] STA disconnected. reason=unknown");
      }
      xEventGroupSetBits(s_wifiEventGroup, kFailBit);
    } else if (event_id == WIFI_EVENT_AP_START) {
      s_apActive = true;
      Serial.println("[WIFI] AP start");
    } else if (event_id == WIFI_EVENT_AP_STOP) {
      s_apActive = false;
      Serial.println("[WIFI] AP stop");
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
      auto *ap_connected = static_cast<wifi_event_ap_staconnected_t *>(event_data);
      if (ap_connected) {
        Serial.printf("[WIFI] AP client connected: %02x:%02x:%02x:%02x:%02x:%02x, AID=%d\n",
                      ap_connected->mac[0], ap_connected->mac[1], ap_connected->mac[2],
                      ap_connected->mac[3], ap_connected->mac[4], ap_connected->mac[5],
                      ap_connected->aid);
      } else {
        Serial.println("[WIFI] AP client connected");
      }
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
      auto *ap_disconnected = static_cast<wifi_event_ap_stadisconnected_t *>(event_data);
      if (ap_disconnected) {
        Serial.printf("[WIFI] AP client disconnected: %02x:%02x:%02x:%02x:%02x:%02x, AID=%d\n",
                      ap_disconnected->mac[0], ap_disconnected->mac[1], ap_disconnected->mac[2],
                      ap_disconnected->mac[3], ap_disconnected->mac[4], ap_disconnected->mac[5],
                      ap_disconnected->aid);
      } else {
        Serial.println("[WIFI] AP client disconnected");
      }
    }
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    s_staConnected = true;
    auto *got_ip = static_cast<ip_event_got_ip_t *>(event_data);
    if (got_ip) {
      Serial.printf("[WIFI] STA got IP: %s\n",
            ip4addr_ntoa(reinterpret_cast<ip4_addr_t *>(&got_ip->ip_info.ip)));
    }
    updateMdnsForNetif(s_staNetif,
                       static_cast<mdns_event_actions_t>(MDNS_EVENT_ENABLE_IP4 |
                                                         MDNS_EVENT_ANNOUNCE_IP4),
                       "STA");
    xEventGroupSetBits(s_wifiEventGroup, kConnectedBit);
  }
}

bool ensureWifiInit() {
  if (s_initialized) {
    return true;
  }

  if (esp_netif_init() != ESP_OK) {
    return false;
  }

  esp_err_t loopErr = esp_event_loop_create_default();
  if (loopErr != ESP_OK && loopErr != ESP_ERR_INVALID_STATE) {
    return false;
  }

  s_staNetif = esp_netif_create_default_wifi_sta();
  s_apNetif = esp_netif_create_default_wifi_ap();
  if (!s_staNetif || !s_apNetif) {
    return false;
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK) {
    return false;
  }

  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler,
                             nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler,
                             nullptr);

  s_wifiEventGroup = xEventGroupCreate();
  if (!s_wifiEventGroup) {
    return false;
  }

  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  s_initialized = true;
  return true;
}

void configureApIp() {
  if (!s_apNetif) {
    return;
  }

  esp_netif_ip_info_t ip_info{};
  IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
  IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
  IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

  esp_netif_dhcps_stop(s_apNetif);
  esp_netif_set_ip_info(s_apNetif, &ip_info);
  esp_netif_dhcps_start(s_apNetif);
  updateMdnsForNetif(s_apNetif,
                     static_cast<mdns_event_actions_t>(MDNS_EVENT_ENABLE_IP4 |
                                                       MDNS_EVENT_ANNOUNCE_IP4),
                     "AP");
  Serial.println("[WIFI] AP IP configured: 192.168.4.1");
}

void ensureMdns(const char *hostname) {
  if (s_mdnsInitialized) {
    return;
  }

  esp_err_t err = mdns_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("[ERROR] mDNS init failed: %s\n", esp_err_to_name(err));
    return;
  }

  err = mdns_hostname_set(hostname);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] mDNS hostname_set failed: %s\n", esp_err_to_name(err));
    return;
  }

  err = mdns_instance_name_set("Smoker Controller");
  if (err != ESP_OK) {
    Serial.printf("[WARN] mDNS instance_name_set failed: %s\n", esp_err_to_name(err));
    // Continue - instance name is optional
  }

  // Add HTTP service for AP mode
  err = mdns_service_add("Smoker Controller", "_http", "_tcp", 80, nullptr, 0);
  if (err != ESP_OK && err != ESP_ERR_NO_MEM) {
    Serial.printf("[WARN] mDNS service_add HTTP failed: %s\n", esp_err_to_name(err));
  }

  // Add HTTPS service for STA mode
  err = mdns_service_add("Smoker Controller", "_https", "_tcp", 443, nullptr, 0);
  if (err != ESP_OK && err != ESP_ERR_NO_MEM) {
    Serial.printf("[WARN] mDNS service_add HTTPS failed: %s\n", esp_err_to_name(err));
  }

  s_mdnsInitialized = true;
  Serial.println("[OK] mDNS responder initialized");
  Serial.printf("  Hostname: %s.local\n", hostname);
}
} // namespace

NetworkMode NetworkManager::begin(const WiFiCredentials &creds) {
  credentials = creds;

  if (!ensureWifiInit()) {
    Serial.println("[ERROR] WiFi hardware init failed, attempting AP mode recovery...");
    // Even if WiFi init fails, try AP as fallback
    // This handles transient initialization errors
    if (startAP()) {
      currentMode = NetworkMode::AP;
      return NetworkMode::AP;
    }
    currentMode = NetworkMode::NONE;
    return NetworkMode::NONE;
  }

  ensureMdns("smoker");

  if (credentials.isEmpty()) {
    Serial.println("[INFO] No WiFi credentials found");
    Serial.println("[INFO] Starting AP mode for configuration...");
    if (startAP()) {
      currentMode = NetworkMode::AP;
      return NetworkMode::AP;
    }
    currentMode = NetworkMode::NONE;
    return NetworkMode::NONE;
  }

  if (connectSTA()) {
    currentMode = NetworkMode::STA;
    return NetworkMode::STA;
  }

  Serial.println("[WARN] STA connection failed, starting AP mode...");
  if (startAP()) {
    currentMode = NetworkMode::AP;
    return NetworkMode::AP;
  }

  currentMode = NetworkMode::NONE;
  return NetworkMode::NONE;
}

bool NetworkManager::connectSTA() {
  Serial.println("\n===========================================");
  Serial.println("Connecting to WiFi (STA mode)");
  Serial.println("===========================================");

  wifi_config_t wifi_config{};
  std::strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid),
               credentials.ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char *>(wifi_config.sta.password),
               credentials.password.c_str(), sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
  wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

  if (credentials.useDHCP) {
    Serial.println("[INFO] Using DHCP for IP assignment");
    esp_netif_dhcpc_start(s_staNetif);
  } else {
    Serial.println("[INFO] Using static IP configuration");

    if (!localIP.fromString(credentials.ip.c_str())) {
      Serial.println("[ERROR] Invalid IP address format");
      return false;
    }

    if (!localGateway.fromString(credentials.gateway.c_str())) {
      Serial.println("[ERROR] Invalid gateway address format");
      return false;
    }

    subnet.fromString(DEFAULT_SUBNET_MASK);

    esp_netif_ip_info_t ip_info{};
    ip_info.ip.addr = localIP.raw().addr;
    ip_info.gw.addr = localGateway.raw().addr;
    ip_info.netmask.addr = subnet.raw().addr;

    esp_netif_dhcpc_stop(s_staNetif);
    esp_netif_set_ip_info(s_staNetif, &ip_info);
  }

    auto tryConnect = [&](bool pmfRequired) -> bool {
      wifi_config.sta.pmf_cfg.capable = true;
      wifi_config.sta.pmf_cfg.required = pmfRequired;
  #if defined(WIFI_AUTH_WPA2_WPA3_PSK)
      wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
  #else
      wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  #endif
  #if defined(WPA3_SAE_PWE_BOTH)
      wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  #elif defined(WIFI_SAE_PWE_BOTH)
      wifi_config.sta.sae_pwe_h2e = WIFI_SAE_PWE_BOTH;
  #endif

      Serial.printf("[WIFI] STA connect attempt (PMF %s)\n",
                    pmfRequired ? "required" : "optional");

      esp_wifi_set_mode(WIFI_MODE_APSTA);
      esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
      esp_wifi_start();
      esp_wifi_connect();

      Serial.printf("Connecting to SSID: %s\n", credentials.ssid.c_str());

      EventBits_t bits = xEventGroupWaitBits(
          s_wifiEventGroup, kConnectedBit | kFailBit, pdTRUE, pdFALSE,
          pdMS_TO_TICKS(WIFI_TIMEOUT_MS));

      if (bits & kConnectedBit) {
        Serial.println("\n[OK] WiFi connected!");
        startFallbackAP();
        return true;
      }

      Serial.printf("[ERROR] WiFi connection timeout (last reason=%d, %s)\n",
                    s_lastDisconnectReason,
                    wifiDisconnectReasonToString(s_lastDisconnectReason));
      esp_wifi_disconnect();
      esp_wifi_stop();
      return false;
    };

    if (tryConnect(false)) {
      return true;
    }

    return tryConnect(true);
}

bool NetworkManager::startFallbackAP() {
  wifi_config_t ap_config{};
  std::strncpy(reinterpret_cast<char *>(ap_config.ap.ssid), AP_SSID,
               sizeof(ap_config.ap.ssid) - 1);
  std::strncpy(reinterpret_cast<char *>(ap_config.ap.password), AP_PASSWORD,
               sizeof(ap_config.ap.password) - 1);
  ap_config.ap.ssid[sizeof(ap_config.ap.ssid) - 1] = '\0';
  ap_config.ap.password[sizeof(ap_config.ap.password) - 1] = '\0';
  ap_config.ap.ssid_len = std::strlen(AP_SSID);
  ap_config.ap.channel = 1;
  ap_config.ap.max_connection = 4;
  ap_config.ap.authmode =
      (std::strlen(AP_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

  esp_wifi_set_mode(WIFI_MODE_APSTA);
  esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  configureApIp();
  Serial.printf("[WIFI] AP SSID: %s (%s)\n", AP_SSID,
                (std::strlen(AP_PASSWORD) == 0) ? "OPEN" : "WPA2");
  return true;
}

bool NetworkManager::startAP() {
  wifi_config_t ap_config{};
  std::strncpy(reinterpret_cast<char *>(ap_config.ap.ssid), AP_SSID,
               sizeof(ap_config.ap.ssid) - 1);
  std::strncpy(reinterpret_cast<char *>(ap_config.ap.password), AP_PASSWORD,
               sizeof(ap_config.ap.password) - 1);
  ap_config.ap.ssid[sizeof(ap_config.ap.ssid) - 1] = '\0';
  ap_config.ap.password[sizeof(ap_config.ap.password) - 1] = '\0';
  ap_config.ap.ssid_len = std::strlen(AP_SSID);
  ap_config.ap.channel = 1;
  ap_config.ap.max_connection = 4;
  ap_config.ap.authmode =
      (std::strlen(AP_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

  esp_err_t stopErr = esp_wifi_stop();
  if (stopErr != ESP_OK && stopErr != ESP_ERR_WIFI_NOT_STARTED) {
    Serial.println("[ERROR] Failed to stop WiFi");
    return false;
  }
  if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
    Serial.println("[ERROR] Failed to set AP mode");
    return false;
  }
  if (esp_wifi_set_config(WIFI_IF_AP, &ap_config) != ESP_OK) {
    Serial.println("[ERROR] Failed to set AP config");
    return false;
  }
  if (esp_wifi_start() != ESP_OK) {
    Serial.println("[ERROR] Failed to start WiFi");
    return false;
  }
  configureApIp();
  Serial.printf("[WIFI] AP SSID: %s (%s)\n", AP_SSID,
                (std::strlen(AP_PASSWORD) == 0) ? "OPEN" : "WPA2");
  Serial.println("[OK] AP mode started successfully");
  return true;
}

bool NetworkManager::isConnected() const {
  return s_staConnected;
}

bool NetworkManager::isAccessPointActive() const {
  return s_apActive;
}

IPAddress NetworkManager::getLocalIP() const {
  esp_netif_ip_info_t ip_info{};
  if (isConnected() && s_staNetif &&
      esp_netif_get_ip_info(s_staNetif, &ip_info) == ESP_OK) {
    return IPAddress(ip4_addr1(&ip_info.ip), ip4_addr2(&ip_info.ip),
                     ip4_addr3(&ip_info.ip), ip4_addr4(&ip_info.ip));
  }
  if (s_apNetif && esp_netif_get_ip_info(s_apNetif, &ip_info) == ESP_OK) {
    return IPAddress(ip4_addr1(&ip_info.ip), ip4_addr2(&ip_info.ip),
                     ip4_addr3(&ip_info.ip), ip4_addr4(&ip_info.ip));
  }
  return IPAddress();
}

String NetworkManager::getSSID() const {
  wifi_ap_record_t ap_info{};
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
    return String(reinterpret_cast<const char *>(ap_info.ssid));
  }
  return String();
}

int NetworkManager::getRSSI() const {
  wifi_ap_record_t ap_info{};
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
    return ap_info.rssi;
  }
  return 0;
}

int NetworkManager::scanNetworks() {
  if (!ensureWifiInit()) {
    return 0;
  }

  wifi_scan_config_t scan_config{};
  esp_wifi_set_mode(WIFI_MODE_APSTA);
  esp_wifi_start();
  esp_wifi_scan_start(&scan_config, true);

  uint16_t count = 0;
  esp_wifi_scan_get_ap_num(&count);
  scannedNetworks.clear();
  scannedNetworks.resize(count);
  if (count > 0) {
    esp_wifi_scan_get_ap_records(&count, scannedNetworks.data());
  }
  return static_cast<int>(count);
}

String NetworkManager::getScannedSSID(int index) {
  if (index < 0 || index >= static_cast<int>(scannedNetworks.size())) {
    return String();
  }
  return String(reinterpret_cast<const char *>(scannedNetworks[index].ssid));
}

int NetworkManager::getScannedRSSI(int index) {
  if (index < 0 || index >= static_cast<int>(scannedNetworks.size())) {
    return 0;
  }
  return scannedNetworks[index].rssi;
}

bool NetworkManager::isScannedNetworkOpen(int index) {
  if (index < 0 || index >= static_cast<int>(scannedNetworks.size())) {
    return false;
  }
  return scannedNetworks[index].authmode == WIFI_AUTH_OPEN;
}
