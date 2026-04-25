#ifndef CAUTH_CORE_PLATFORM_WEBSOCKET_CLIENT_HPP
#define CAUTH_CORE_PLATFORM_WEBSOCKET_CLIENT_HPP

#include "core/platform/operation_cancel.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cauth::core::platform {

struct WebSocketRequest {
    std::string host;
    std::uint16_t port = 0;
    std::string path;
    bool secure = true;
    std::int32_t connect_timeout_ms = 5000;
    std::int32_t receive_timeout_ms = 10000;
    OperationCancelContext cancel_context;
};

struct WebSocketProbeResult {
    bool ok = false;
    std::string error_message;
};

struct WebSocketReceiveResult {
    bool ok = false;
    std::string error_message;
    std::vector<std::uint8_t> bytes;
};

class WebSocketConnection {
  public:
    virtual ~WebSocketConnection() = default;

    virtual WebSocketProbeResult send_binary(const std::vector<std::uint8_t>& bytes) = 0;
    virtual WebSocketReceiveResult receive() = 0;
    virtual void close() = 0;
};

class WebSocketClient {
  public:
    WebSocketProbeResult probe(const WebSocketRequest& request) const;
    std::pair<WebSocketProbeResult, std::unique_ptr<WebSocketConnection>>
    connect(const WebSocketRequest& request) const;
};

std::string build_websocket_url(const WebSocketRequest& request);
bool is_valid_websocket_request(const WebSocketRequest& request);

} // namespace cauth::core::platform

#endif
