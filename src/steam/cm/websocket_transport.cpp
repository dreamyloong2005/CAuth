#include "steam/cm/websocket_transport.hpp"

#include "core/platform/operation_cancel.hpp"

namespace cauth::core::cm {

std::string cm_websocket_path() {
    return "/cmsocket/";
}

bool is_valid_websocket_endpoint(const CmServerEndpoint& endpoint) {
    auto request = cauth::core::platform::WebSocketRequest{};
    request.host = endpoint.address;
    request.port = endpoint.port;
    request.path = cm_websocket_path();
    request.secure = true;
    return endpoint.protocol == CmServerProtocol::WebSocket &&
           cauth::core::platform::is_valid_websocket_request(request);
}

std::pair<CmWebSocketProbeResult, std::unique_ptr<CmWebSocketConnection>>
CmWebSocketTransport::connect(const CmServerEndpoint& endpoint) const {
    if (!is_valid_websocket_endpoint(endpoint)) {
        return std::make_pair(CmWebSocketProbeResult{false, "invalid websocket CM endpoint"},
                              std::unique_ptr<CmWebSocketConnection>{});
    }

    auto request = cauth::core::platform::WebSocketRequest{};
    request.host = endpoint.address;
    request.port = endpoint.port;
    request.path = cm_websocket_path();
    request.secure = true;
    request.cancel_context = cauth::core::platform::current_thread_operation_cancel_context();
    return client_.connect(request);
}

CmWebSocketProbeResult CmWebSocketTransport::probe(const CmServerEndpoint& endpoint) const {
    auto request = cauth::core::platform::WebSocketRequest{};
    request.host = endpoint.address;
    request.port = endpoint.port;
    request.path = cm_websocket_path();
    request.secure = true;
    request.cancel_context = cauth::core::platform::current_thread_operation_cancel_context();
    return client_.probe(request);
}

} // namespace cauth::core::cm
