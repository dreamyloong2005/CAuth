#include "steam/cm/steam_cm_connector.hpp"

#include "steam/cm/cm_client_hello.hpp"
#include "steam/cm/cm_message.hpp"
#include "steam/cm/steam_directory.hpp"
#include "steam/cm/websocket_transport.hpp"

#include <memory>
#include <ostream>
#include <utility>

namespace cauth::core::cm {
namespace {

void write_line(std::ostream* stream, const std::string& message) {
    if (stream != nullptr) {
        *stream << message << '\n';
    }
}

std::string describe_cm_logon_failure(const CmSessionConnectResult& logon) {
    if (logon.logon_response.eresult == 0) {
        return logon.error_message;
    }

    std::string message = "CM logon failed with eresult " +
                          std::to_string(logon.logon_response.eresult);
    if (logon.logon_response.eresult_extended != 0) {
        message += " (extended " +
                   std::to_string(logon.logon_response.eresult_extended) + ")";
    }
    if (!logon.logon_response.error_message.empty()) {
        message += ": ";
        message += logon.logon_response.error_message;
    }
    return message;
}

std::vector<CmServerEndpoint> built_in_websocket_servers(std::uint32_t max_count) {
    static constexpr struct {
        std::string_view host;
        std::uint16_t port;
    } kFallbackServers[] = {
        {"cmp1-hkg1.steamserver.net", 27018},
        {"cmp1-hkg1.steamserver.net", 27022},
        {"cmp1-hkg1.steamserver.net", 27023},
        {"cmp1-hkg1.steamserver.net", 27024},
        {"cmp1-hkg1.steamserver.net", 27025},
        {"cmp2-hkg1.steamserver.net", 27018},
        {"cmp2-hkg1.steamserver.net", 27020},
        {"cmp2-hkg1.steamserver.net", 27021},
        {"cmp2-hkg1.steamserver.net", 27022},
        {"cmp2-hkg1.steamserver.net", 27024},
        {"cmp2-hkg1.steamserver.net", 27025},
        {"cmp3-hkg1.steamserver.net", 27018},
        {"cmp3-hkg1.steamserver.net", 27022},
        {"cmp3-hkg1.steamserver.net", 27023},
        {"cmp3-hkg1.steamserver.net", 27024},
        {"cmp3-hkg1.steamserver.net", 27025},
    };

    std::vector<CmServerEndpoint> servers;
    const auto limit =
        max_count == 0 ? std::size(kFallbackServers)
                       : std::min<std::size_t>(max_count, std::size(kFallbackServers));
    servers.reserve(limit);
    for (std::size_t index = 0; index < limit; ++index) {
        servers.push_back(CmServerEndpoint{
            std::string{kFallbackServers[index].host},
            kFallbackServers[index].port,
            CmServerProtocol::WebSocket,
        });
    }
    return servers;
}

CmServerListResult load_websocket_servers(std::uint32_t max_count) {
    SteamDirectoryClient directory;
    CmServerQuery query;
    query.protocol = CmServerProtocol::WebSocket;
    query.max_count = max_count;
    auto result = directory.get_cm_servers(query);
    if (result.ok) {
        return result;
    }

    auto fallback = built_in_websocket_servers(max_count);
    if (fallback.empty()) {
        return result;
    }

    result.ok = true;
    result.error_message = "Steam Directory unavailable; using built-in CM websocket fallback: " +
                           result.error_message;
    result.servers = std::move(fallback);
    return result;
}

} // namespace

SteamCmConnector::SteamCmConnector(std::ostream* out, std::ostream* err) : out_(out), err_(err) {}

SteamCmOperationResult SteamCmConnector::with_service_client(
    std::uint32_t max_count,
    const SteamCmServiceClientOperation& operation) const {
    const auto servers = load_websocket_servers(max_count);
    if (!servers.ok) {
        return {false, "CM directory lookup failed: " + servers.error_message};
    }
    if (!servers.error_message.empty()) {
        write_line(err_, servers.error_message);
    }

    CmWebSocketTransport transport;
    std::string last_error = "CM service connection failed for all endpoints";
    for (const auto& server : servers.servers) {
        write_line(out_, "Connecting CM websocket " + server.address + ":" + std::to_string(server.port) +
                             "...");
        auto [connect_result, connection] = transport.connect(server);
        if (!connect_result.ok || !connection) {
            last_error = connect_result.error_message;
            write_line(err_, "Connect failed: " + connect_result.error_message);
            continue;
        }

        const auto hello = make_client_hello_message();
        const auto hello_result = connection->send_binary(encode_cm_message(hello));
        if (!hello_result.ok) {
            last_error = hello_result.error_message;
            write_line(err_, "ClientHello send failed: " + hello_result.error_message);
            connection->close();
            continue;
        }

        CmServiceMethodClient service_client{*connection};
        const auto attempt = operation(server, service_client);
        connection->close();

        if (!attempt.error_message.empty()) {
            last_error = attempt.error_message;
        }

        if (attempt.continuation == SteamCmContinuation::Stop) {
            return {attempt.ok, attempt.error_message};
        }
    }

    return {false, last_error};
}

SteamCmOperationResult SteamCmConnector::with_logged_on_session(
    const cauth::core::session::AuthSession& session,
    std::uint32_t max_count,
    const SteamCmSessionOperation& operation) const {
    const auto servers = load_websocket_servers(max_count);
    if (!servers.ok) {
        return {false, "CM directory lookup failed: " + servers.error_message};
    }
    if (!servers.error_message.empty()) {
        write_line(err_, servers.error_message);
    }

    CmWebSocketTransport transport;
    std::string last_error = "CM logon failed for all endpoints";
    for (const auto& server : servers.servers) {
        write_line(out_, "Connecting CM websocket " + server.address + ":" + std::to_string(server.port) +
                             "...");
        auto [connect_result, connection] = transport.connect(server);
        if (!connect_result.ok || !connection) {
            last_error = connect_result.error_message;
            write_line(err_, "Connect failed: " + connect_result.error_message);
            continue;
        }

        CmSession cm_session{std::move(connection)};
        write_line(out_, "ClientLogon sent; waiting for response...");
        const auto logon = cm_session.logon(session);
        if (!logon.ok) {
            last_error = describe_cm_logon_failure(logon);
            write_line(err_, last_error);
            continue;
        }

        const auto heartbeat_result = cm_session.send_heartbeat(session);
        if (!heartbeat_result.ok) {
            return {false, "CM heartbeat send failed: " + heartbeat_result.error_message};
        }

        const auto attempt = operation(server, cm_session);
        if (!attempt.error_message.empty()) {
            last_error = attempt.error_message;
        }

        if (attempt.continuation == SteamCmContinuation::Stop) {
            return {attempt.ok, attempt.error_message};
        }
    }

    return {false, last_error};
}

} // namespace cauth::core::cm
