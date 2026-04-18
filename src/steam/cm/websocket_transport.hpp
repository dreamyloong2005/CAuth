#ifndef CAUTH_CORE_CM_WEBSOCKET_TRANSPORT_HPP
#define CAUTH_CORE_CM_WEBSOCKET_TRANSPORT_HPP

#include "core/platform/websocket_client.hpp"
#include "steam/cm/cm_server.hpp"

#include <memory>
#include <string>
#include <utility>

namespace cauth::core::cm {

using CmWebSocketProbeResult = cauth::core::platform::WebSocketProbeResult;
using CmWebSocketReceiveResult = cauth::core::platform::WebSocketReceiveResult;
using CmWebSocketConnection = cauth::core::platform::WebSocketConnection;

class CmWebSocketTransport {
  public:
    CmWebSocketProbeResult probe(const CmServerEndpoint& endpoint) const;
    std::pair<CmWebSocketProbeResult, std::unique_ptr<CmWebSocketConnection>>
    connect(const CmServerEndpoint& endpoint) const;

  private:
    cauth::core::platform::WebSocketClient client_;
};

std::string cm_websocket_path();
bool is_valid_websocket_endpoint(const CmServerEndpoint& endpoint);

} // namespace cauth::core::cm

#endif
