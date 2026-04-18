#include "steam/cm/steam_directory.hpp"

#include <iostream>

int main() {
    const auto websocket_servers = cauth::core::cm::parse_cm_server_list_response(
        R"({"response":{"serverlist_websockets":["wss://cm1.example.com:443/cmsocket/","wss://cm2.example.com/cmsocket/"]}})",
        cauth::core::cm::CmServerProtocol::WebSocket);
    if (websocket_servers.size() != 2 || websocket_servers[0].address != "cm1.example.com" ||
        websocket_servers[0].port != 443 ||
        websocket_servers[0].protocol != cauth::core::cm::CmServerProtocol::WebSocket ||
        websocket_servers[1].address != "cm2.example.com" ||
        websocket_servers[1].port != 443) {
        std::cerr << "websocket CM server list should parse host and port\n";
        return 1;
    }

    const auto tcp_servers = cauth::core::cm::parse_cm_server_list_response(
        R"({"response":{"serverlist":["127.0.0.1:27017","10.0.0.1:27018"]}})",
        cauth::core::cm::CmServerProtocol::Tcp);
    if (tcp_servers.size() != 2 || tcp_servers[0].address != "127.0.0.1" ||
        tcp_servers[0].port != 27017 ||
        tcp_servers[0].protocol != cauth::core::cm::CmServerProtocol::Tcp) {
        std::cerr << "tcp CM server list should parse host and port\n";
        return 1;
    }

    const auto object_servers = cauth::core::cm::parse_cm_server_list_response(
        R"({"response":{"serverlist":[{"endpoint":"wss://cmp1.example.net:443/cmsocket/","legacy_endpoint":"155.133.1.2:27017","type":"websockets"}]}})",
        cauth::core::cm::CmServerProtocol::WebSocket);
    if (object_servers.size() != 1 || object_servers[0].address != "cmp1.example.net" ||
        object_servers[0].port != 443) {
        std::cerr << "object CM server list should parse endpoint fields\n";
        return 1;
    }

    const auto host_port_websocket_servers = cauth::core::cm::parse_cm_server_list_response(
        R"({"response":{"serverlist":[{"endpoint":"cmp1.example.net:27022","legacy_endpoint":"cmp2.example.net:27022","type":"websockets"}]}})",
        cauth::core::cm::CmServerProtocol::WebSocket);
    if (host_port_websocket_servers.size() != 1 ||
        host_port_websocket_servers[0].address != "cmp1.example.net" ||
        host_port_websocket_servers[0].port != 27022 ||
        host_port_websocket_servers[0].protocol !=
            cauth::core::cm::CmServerProtocol::WebSocket) {
        std::cerr << "websocket object endpoints may be returned as host:port\n";
        return 1;
    }

    return 0;
}
