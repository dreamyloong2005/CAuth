#include "steam/cm/websocket_transport.hpp"

#include <iostream>

int main() {
    if (cauth::core::cm::cm_websocket_path() != "/cmsocket/") {
        std::cerr << "CM websocket path should match Steam CM websocket endpoint\n";
        return 1;
    }

    if (cauth::core::cm::is_valid_websocket_endpoint(
            {"", 27020, cauth::core::cm::CmServerProtocol::WebSocket})) {
        std::cerr << "empty websocket host should be invalid\n";
        return 1;
    }

    if (cauth::core::cm::is_valid_websocket_endpoint(
            {"cmp.example.net", 0, cauth::core::cm::CmServerProtocol::WebSocket})) {
        std::cerr << "zero websocket port should be invalid\n";
        return 1;
    }

    if (cauth::core::cm::is_valid_websocket_endpoint(
            {"cmp.example.net", 27020, cauth::core::cm::CmServerProtocol::Tcp})) {
        std::cerr << "tcp endpoint should not be valid for websocket transport\n";
        return 1;
    }

    if (!cauth::core::cm::is_valid_websocket_endpoint(
            {"cmp.example.net", 27020, cauth::core::cm::CmServerProtocol::WebSocket})) {
        std::cerr << "valid websocket endpoint should be accepted\n";
        return 1;
    }

    return 0;
}
