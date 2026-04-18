#include "steam/cm/websocket_transport.hpp"

namespace cauth::core::cm {

std::string cm_websocket_path() {
    return "/cmsocket/";
}

bool is_valid_websocket_endpoint(const CmServerEndpoint& endpoint) {
    return endpoint.protocol == CmServerProtocol::WebSocket &&
           cauth::core::platform::is_valid_websocket_request(
               {endpoint.address, endpoint.port, cm_websocket_path()});
}

std::pair<CmWebSocketProbeResult, std::unique_ptr<CmWebSocketConnection>>
CmWebSocketTransport::connect(const CmServerEndpoint& endpoint) const {
    if (!is_valid_websocket_endpoint(endpoint)) {
        return std::make_pair(CmWebSocketProbeResult{false, "invalid websocket CM endpoint"},
                              std::unique_ptr<CmWebSocketConnection>{});
    }

    return client_.connect({endpoint.address, endpoint.port, cm_websocket_path(), true});
}

CmWebSocketProbeResult CmWebSocketTransport::probe(const CmServerEndpoint& endpoint) const {
    return client_.probe({endpoint.address, endpoint.port, cm_websocket_path(), true});
}

} // namespace cauth::core::cm
