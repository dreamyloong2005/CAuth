#include "steam/cm/steam_cm_connector.hpp"

#include "steam/cm/cm_client_hello.hpp"
#include "core/platform/endpoint_route_cache.hpp"
#include "core/platform/operation_cancel.hpp"
#include "core/platform/route_selection.hpp"
#include "steam/cm/cm_message.hpp"
#include "steam/cm/steam_directory.hpp"
#include "steam/cm/websocket_transport.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <ostream>
#include <utility>

namespace cauth::core::cm {
namespace {

void write_line(std::ostream* stream, const std::string& message) {
    if (stream != nullptr) {
        *stream << message << '\n';
    }
}

std::string cm_endpoint_route_key(const CmServerEndpoint& endpoint) {
    return endpoint.address + ":" + std::to_string(endpoint.port);
}

bool is_operation_canceled(std::string_view message) {
    return message.find("operation canceled") != std::string_view::npos;
}

std::vector<CmServerEndpoint> rank_websocket_servers(std::vector<CmServerEndpoint> servers) {
    if (servers.size() <= 1) {
        return servers;
    }

    return cauth::core::platform::rank_endpoints_by_route_health(
        "steam.cm.websocket",
        std::move(servers),
        [](const CmServerEndpoint& endpoint) { return cm_endpoint_route_key(endpoint); },
        [](const CmServerEndpoint& endpoint) {
            CmWebSocketTransport transport;
            const auto started = std::chrono::steady_clock::now();
            const auto result = transport.probe(endpoint);
            if (!result.ok) {
                return cauth::core::platform::EndpointProbeOutcome::failed();
            }
            return cauth::core::platform::EndpointProbeOutcome::succeeded(
                cauth::core::platform::elapsed_milliseconds(
                    std::chrono::steady_clock::now() - started));
        },
        6);
}

std::optional<std::vector<CmServerEndpoint>> select_websocket_servers(
    std::vector<CmServerEndpoint> servers,
    const cauth::core::platform::RouteSelection* route_selection,
    std::string& error_message) {
    if (route_selection == nullptr || route_selection->empty()) {
        return servers;
    }

    std::vector<CmServerEndpoint> selected;
    selected.reserve(servers.size());
    for (auto& server : servers) {
        if (cauth::core::platform::route_selection_matches(
                route_selection,
                server.address + ":" + std::to_string(server.port),
                "websocket")) {
            selected.push_back(std::move(server));
        }
    }
    if (!selected.empty()) {
        return selected;
    }

    error_message = "selected CM route is not available: " + route_selection->endpoint;
    if (!route_selection->protocol.empty()) {
        error_message += " (protocol=" + route_selection->protocol + ")";
    }
    return std::nullopt;
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

SteamCmRouteReport probe_websocket_routes(std::uint32_t max_count,
                                          std::ostream* err,
                                          const cauth::core::platform::RouteSelection* route_selection) {
    SteamCmRouteReport report;
    auto servers = load_websocket_servers(max_count);
    if (!servers.ok) {
        report.ok = false;
        report.module_status = "failed";
        report.message = "CM directory lookup failed: " + servers.error_message;
        return report;
    }
    if (!servers.error_message.empty()) {
        report.message = servers.error_message;
        if (err != nullptr) {
            write_line(err, servers.error_message);
        }
    } else {
        report.message = "ok";
    }

    std::string selection_error;
    auto selected_servers = select_websocket_servers(
        std::move(servers.servers), route_selection, selection_error);
    if (!selected_servers.has_value()) {
        report.ok = false;
        report.module_status = "failed";
        report.message = selection_error;
        return report;
    }

    servers.servers = rank_websocket_servers(std::move(*selected_servers));
    auto& cache = cauth::core::platform::EndpointRouteCache::instance();
    report.ok = true;
    report.module_status = "succeeded";
    report.routes.reserve(servers.servers.size());
    for (const auto& server : servers.servers) {
        const auto snapshot = cache.snapshot("steam.cm.websocket", cm_endpoint_route_key(server));
        report.routes.push_back(SteamCmRouteEntry{
            server,
            cauth::core::platform::make_probed_route_entry(
                server.address + ":" + std::to_string(server.port),
                "websocket",
                "control",
                {},
                snapshot),
        });
    }
    return report;
}

SteamCmConnector::SteamCmConnector(std::ostream* out, std::ostream* err) : out_(out), err_(err) {}

SteamCmOperationResult SteamCmConnector::with_service_client(
    std::uint32_t max_count,
    const cauth::core::platform::RouteSelection* route_selection,
    const SteamCmServiceClientOperation& operation) const {
    if (cauth::core::platform::current_thread_operation_cancel_requested()) {
        return {false, "operation canceled"};
    }
    auto servers = load_websocket_servers(max_count);
    if (!servers.ok) {
        return {false, "CM directory lookup failed: " + servers.error_message};
    }
    if (!servers.error_message.empty()) {
        write_line(err_, servers.error_message);
    }
    std::string selection_error;
    auto selected_servers = select_websocket_servers(
        std::move(servers.servers), route_selection, selection_error);
    if (!selected_servers.has_value()) {
        return {false, selection_error};
    }
    servers.servers = rank_websocket_servers(std::move(*selected_servers));

    CmWebSocketTransport transport;
    std::string last_error = "CM service connection failed for all endpoints";
    for (const auto& server : servers.servers) {
        if (cauth::core::platform::current_thread_operation_cancel_requested()) {
            return {false, "operation canceled"};
        }
        write_line(out_, "Connecting CM websocket " + server.address + ":" + std::to_string(server.port) +
                             "...");
        const auto connect_started = std::chrono::steady_clock::now();
        auto [connect_result, connection] = transport.connect(server);
        if (!connect_result.ok || !connection) {
            if (is_operation_canceled(connect_result.error_message)) {
                return {false, "operation canceled"};
            }
            cauth::core::platform::EndpointRouteCache::instance().record_failure(
                "steam.cm.websocket", cm_endpoint_route_key(server));
            last_error = connect_result.error_message;
            write_line(err_, "Connect failed: " + connect_result.error_message);
            continue;
        }
        cauth::core::platform::EndpointRouteCache::instance().record_success(
            "steam.cm.websocket",
            cm_endpoint_route_key(server),
            cauth::core::platform::elapsed_milliseconds(
                std::chrono::steady_clock::now() - connect_started));

        const auto hello = make_client_hello_message();
        const auto hello_result = connection->send_binary(encode_cm_message(hello));
        if (!hello_result.ok) {
            if (is_operation_canceled(hello_result.error_message)) {
                connection->close();
                return {false, "operation canceled"};
            }
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

        if (is_operation_canceled(attempt.error_message)) {
            return {false, "operation canceled"};
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
    const cauth::core::platform::RouteSelection* route_selection,
    const SteamCmSessionOperation& operation) const {
    if (cauth::core::platform::current_thread_operation_cancel_requested()) {
        return {false, "operation canceled"};
    }
    auto servers = load_websocket_servers(max_count);
    if (!servers.ok) {
        return {false, "CM directory lookup failed: " + servers.error_message};
    }
    if (!servers.error_message.empty()) {
        write_line(err_, servers.error_message);
    }
    std::string selection_error;
    auto selected_servers = select_websocket_servers(
        std::move(servers.servers), route_selection, selection_error);
    if (!selected_servers.has_value()) {
        return {false, selection_error};
    }
    servers.servers = rank_websocket_servers(std::move(*selected_servers));

    CmWebSocketTransport transport;
    std::string last_error = "CM logon failed for all endpoints";
    for (const auto& server : servers.servers) {
        if (cauth::core::platform::current_thread_operation_cancel_requested()) {
            return {false, "operation canceled"};
        }
        write_line(out_, "Connecting CM websocket " + server.address + ":" + std::to_string(server.port) +
                             "...");
        const auto connect_started = std::chrono::steady_clock::now();
        auto [connect_result, connection] = transport.connect(server);
        if (!connect_result.ok || !connection) {
            if (is_operation_canceled(connect_result.error_message)) {
                return {false, "operation canceled"};
            }
            cauth::core::platform::EndpointRouteCache::instance().record_failure(
                "steam.cm.websocket", cm_endpoint_route_key(server));
            last_error = connect_result.error_message;
            write_line(err_, "Connect failed: " + connect_result.error_message);
            continue;
        }
        cauth::core::platform::EndpointRouteCache::instance().record_success(
            "steam.cm.websocket",
            cm_endpoint_route_key(server),
            cauth::core::platform::elapsed_milliseconds(
                std::chrono::steady_clock::now() - connect_started));

        CmSession cm_session{std::move(connection)};
        write_line(out_, "ClientLogon sent; waiting for response...");
        const auto logon = cm_session.logon(session);
        if (!logon.ok) {
            last_error = describe_cm_logon_failure(logon);
            if (is_operation_canceled(last_error)) {
                return {false, "operation canceled"};
            }
            write_line(err_, last_error);
            continue;
        }

        const auto heartbeat_result = cm_session.send_heartbeat(session);
        if (!heartbeat_result.ok) {
            if (is_operation_canceled(heartbeat_result.error_message)) {
                return {false, "operation canceled"};
            }
            return {false, "CM heartbeat send failed: " + heartbeat_result.error_message};
        }

        const auto attempt = operation(server, cm_session);
        if (!attempt.error_message.empty()) {
            last_error = attempt.error_message;
        }

        if (is_operation_canceled(attempt.error_message)) {
            return {false, "operation canceled"};
        }
        if (attempt.continuation == SteamCmContinuation::Stop) {
            return {attempt.ok, attempt.error_message};
        }
    }

    return {false, last_error};
}

} // namespace cauth::core::cm
