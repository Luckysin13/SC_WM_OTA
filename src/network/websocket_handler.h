#ifndef WEBSOCKET_HANDLER_H
#define WEBSOCKET_HANDLER_H

#include "compat/compat.h"
#include "control/state_coordinator.h"
#include "esp_http_server.h"
#include <vector>

// =============================================================================
// WEBSOCKET HANDLER CLASS
// =============================================================================
// Manages WebSocket connections and implements observer pattern
// Handles bidirectional communication with web clients
// =============================================================================

class WebSocketHandler : public IStateObserver {
private:
    httpd_handle_t server = nullptr;
    StateCoordinator& stateCoord;
    std::vector<int> clientFds;

    static esp_err_t handleWs(httpd_req_t* req);
    void handleMessage(const String& message);
    void registerClient(int fd);
    void unregisterClient(int fd);

public:
    WebSocketHandler(StateCoordinator& coordinator);

    void init(httpd_handle_t httpdServer);

    // IStateObserver implementation - called when state changes
    void onStateChanged() override;

    // Send current state or custom JSON to all connected clients
    void updateClients();
    void updateClients(const String& customJson);

    // Cleanup disconnected clients (call in loop)
    void cleanupClients();

    size_t getClientCount() const { return clientFds.size(); }
};

#endif // WEBSOCKET_HANDLER_H
