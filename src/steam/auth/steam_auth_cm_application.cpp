#include "steam/auth/steam_auth_cm_application.hpp"

#include "core/platform/session_repository_factory.hpp"
#include "steam/cm/steam_cm_connector.hpp"
#include "steam/cm/cm_message.hpp"
#include "steam/cm/cm_session.hpp"
#include "steam/cm/websocket_transport.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <utility>
#include <optional>
#include <string>

namespace cauth::steam::auth {
namespace {

std::optional<cauth::core::cm::CmServerListResult> load_servers(
    const cauth::core::cm::CmServerQuery& query,
    std::ostream& err) {
    cauth::core::cm::SteamDirectoryClient directory;
    const auto result = directory.get_cm_servers(query);
    if (!result.ok) {
        err << "CM directory lookup failed: " << result.error_message << '\n';
        return std::nullopt;
    }
    return result;
}

} // namespace

int run_cm_logon_then(const cauth::core::cm::CmServerQuery& query,
                      std::uint64_t steam_id,
                      const cauth::core::platform::RouteSelection* route_selection,
                      const CmLogonContinuation& after_logon,
                      std::ostream& out,
                      std::ostream& err) {
    const auto result = load_servers(query, err);
    if (!result.has_value()) return 1;

    const auto store = cauth::core::platform::make_platform_session_repository();
    auto session = steam_id == 0
                       ? std::nullopt
                       : store->load_auth_session(kSteamAuthProvider, std::to_string(steam_id));
    if (!session.has_value()) {
        err << "Steam auth: account not found; pass --steam-id <id>\n";
        return 1;
    }
    if (session->refresh_token.empty()) {
        err << "Steam auth: refresh token missing; please log in again with the current build\n";
        return 1;
    }

    cauth::core::cm::CmWebSocketTransport transport;
    auto selected_servers = result->servers;
    if (route_selection != nullptr && !route_selection->empty()) {
        std::vector<cauth::core::cm::CmServerEndpoint> filtered;
        filtered.reserve(selected_servers.size());
        for (auto& server : selected_servers) {
            if (cauth::core::platform::route_selection_matches(
                    route_selection,
                    server.address + ":" + std::to_string(server.port),
                    "websocket")) {
                filtered.push_back(std::move(server));
            }
        }
        if (filtered.empty()) {
            err << "selected CM route is not available: " << route_selection->endpoint << '\n';
            return 1;
        }
        selected_servers = std::move(filtered);
    }

    for (const auto& server : selected_servers) {
        out << "Connecting CM websocket " << server.address << ':' << server.port << "...\n";
        auto [connect_result, connection] = transport.connect(server);
        if (!connect_result.ok || !connection) {
            err << "Connect failed: " << connect_result.error_message << '\n';
            continue;
        }

        cauth::core::cm::CmSession cm_session{std::move(connection)};
        out << "ClientLogon sent; waiting for response...\n";
        const auto logon = cm_session.logon(*session);
        if (!logon.ok) {
            if (logon.logon_response.eresult != 0) {
                err << "CM logon failed with eresult " << logon.logon_response.eresult << '\n';
                if (logon.logon_response.eresult_extended != 0) {
                    err << "CM logon extended eresult " << logon.logon_response.eresult_extended << '\n';
                }
                if (!logon.logon_response.diagnostic_fields.empty()) {
                    err << "CM logon response fields:\n";
                    for (const auto& field : logon.logon_response.diagnostic_fields) {
                        err << "  " << field << '\n';
                    }
                }
                return 1;
            }
            err << logon.error_message << '\n';
            continue;
        }

        out << "CM logon: ok";
        if (logon.logon_response.heartbeat_seconds != 0) {
            out << " heartbeat=" << logon.logon_response.heartbeat_seconds << "s";
        }
        out << '\n';
        const auto heartbeat_result = cm_session.send_heartbeat(*session);
        if (!heartbeat_result.ok) {
            err << "CM heartbeat send failed: " << heartbeat_result.error_message << '\n';
            return 1;
        }
        out << "CM heartbeat: sent\n";

        if (!after_logon) return 0;
        return after_logon(cm_session, out, err);
    }

    err << "CM logon failed for all endpoints\n";
    return 1;
}

int run_cm_frame_test(std::ostream& out, std::ostream& err) {
    const cauth::core::cm::CmMessage message{
        cauth::core::cm::EMsg::ClientHeartBeat,
        true,
        {1, 2, 3},
        {4, 5, 6},
    };
    const auto encoded = cauth::core::cm::encode_cm_message(message);
    const auto decoded = cauth::core::cm::decode_cm_message(encoded);
    if (!decoded.has_value() || decoded->emsg != message.emsg ||
        decoded->header != message.header || decoded->body != message.body) {
        err << "CM frame codec: failed\n";
        return 1;
    }
    out << "CM frame codec: ok\n";
    return 0;
}

int run_cm_servers(const cauth::core::cm::CmServerQuery& query,
                   std::ostream& out,
                   std::ostream& err) {
    const auto result = load_servers(query, err);
    if (!result.has_value()) return 1;
    for (const auto& server : result->servers) {
        out << (server.protocol == cauth::core::cm::CmServerProtocol::WebSocket ? "websocket" : "tcp")
            << ' ' << server.address << ':' << server.port << '\n';
    }
    return 0;
}

int run_cm_routes(const cauth::core::cm::CmServerQuery& query,
                  const cauth::core::platform::RouteSelection* route_selection,
                  std::ostream& out,
                  std::ostream& err) {
    if (query.protocol != cauth::core::cm::CmServerProtocol::WebSocket) {
        err << "steam auth cm routes only supports websocket endpoints right now\n";
        return 2;
    }

    const auto report = cauth::core::cm::probe_websocket_routes(query.max_count, &err, route_selection);
    if (!report.ok) {
        err << report.message << '\n';
        return 1;
    }

    out << "CM routes: " << report.routes.size() << '\n';
    for (std::size_t index = 0; index < report.routes.size(); ++index) {
        const auto& route = report.routes[index];
        out << "  [" << (index + 1) << "] " << route.endpoint.address << ':'
            << route.endpoint.port
            << " protocol=" << route.route.protocol
            << " latency="
            << (route.route.latency_known ? std::to_string(route.route.latency_ms) + "ms"
                                          : "unknown")
            << " recent_success=" << (route.route.recent_success ? "true" : "false")
            << " recent_failure=" << (route.route.recent_failure ? "true" : "false")
            << " success_count=" << route.route.success_count
            << " failure_count=" << route.route.failure_count
            << '\n';
    }
    return 0;
}

int run_cm_probe(const cauth::core::cm::CmServerQuery& query,
                 const cauth::core::platform::RouteSelection* route_selection,
                 std::ostream& out,
                 std::ostream& err) {
    const auto result = load_servers(query, err);
    if (!result.has_value()) return 1;

    cauth::core::cm::CmWebSocketTransport transport;
    auto selected_servers = result->servers;
    if (route_selection != nullptr && !route_selection->empty()) {
        std::vector<cauth::core::cm::CmServerEndpoint> filtered;
        filtered.reserve(selected_servers.size());
        for (auto& server : selected_servers) {
            if (cauth::core::platform::route_selection_matches(
                    route_selection,
                    server.address + ":" + std::to_string(server.port),
                    "websocket")) {
                filtered.push_back(std::move(server));
            }
        }
        if (filtered.empty()) {
            err << "selected CM route is not available: " << route_selection->endpoint << '\n';
            return 1;
        }
        selected_servers = std::move(filtered);
    }

    for (const auto& server : selected_servers) {
        out << "Probing websocket " << server.address << ':' << server.port << "...\n";
        const auto probe = transport.probe(server);
        if (probe.ok) {
            out << "CM websocket: connected " << server.address << ':' << server.port << '\n';
            return 0;
        }
        err << "Probe failed: " << probe.error_message << '\n';
    }
    err << "CM websocket probe failed for all endpoints\n";
    return 1;
}

int run_cm_logon(const cauth::core::cm::CmServerQuery& query,
                 std::uint64_t steam_id,
                 const cauth::core::platform::RouteSelection* route_selection,
                 std::ostream& out,
                 std::ostream& err) {
    return run_cm_logon_then(query, steam_id, route_selection, {}, out, err);
}

} // namespace cauth::steam::auth
