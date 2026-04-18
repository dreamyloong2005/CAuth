#include "core/platform/websocket_client.hpp"

#include <iostream>

int main() {
    using cauth::core::platform::WebSocketRequest;

    const WebSocketRequest secure_request{"cmp.example.net", 27020, "/cmsocket/"};
    if (!cauth::core::platform::is_valid_websocket_request(secure_request)) {
        std::cerr << "valid websocket request should be accepted\n";
        return 1;
    }

    if (cauth::core::platform::build_websocket_url(secure_request) !=
        "wss://cmp.example.net:27020/cmsocket/") {
        std::cerr << "secure websocket URL should be built correctly\n";
        return 1;
    }

    if (cauth::core::platform::is_valid_websocket_request({"", 27020, "/cmsocket/"})) {
        std::cerr << "empty host should be rejected\n";
        return 1;
    }

    if (cauth::core::platform::is_valid_websocket_request({"cmp.example.net", 0, "/cmsocket/"})) {
        std::cerr << "zero port should be rejected\n";
        return 1;
    }

    if (cauth::core::platform::is_valid_websocket_request(
            {"cmp.example.net", 27020, "cmsocket/"})) {
        std::cerr << "path without leading slash should be rejected\n";
        return 1;
    }

    const WebSocketRequest plain_request{"localhost", 8080, "/ws", false};
    if (cauth::core::platform::build_websocket_url(plain_request) != "ws://localhost:8080/ws") {
        std::cerr << "plain websocket URL should be built correctly\n";
        return 1;
    }

    return 0;
}
