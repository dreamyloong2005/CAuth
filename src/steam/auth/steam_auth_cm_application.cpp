#include "steam/auth/steam_auth_cm_application.hpp"

#include "core/platform/session_repository_factory.hpp"
#include "steam/cm/cm_message.hpp"
#include "steam/cm/cm_session.hpp"
#include "steam/cm/websocket_transport.hpp"
#include "steam/auth/steam_session_identity.hpp"
#include "steam/depot/app_info.hpp"
#include "steam/depot/pics.hpp"

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

int run_logon_or_app_info(const cauth::core::cm::CmServerQuery& query,
                          std::uint64_t steam_id,
                          std::optional<std::uint32_t> app_id,
                          bool debug_app_info,
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
    for (const auto& server : result->servers) {
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

        if (!app_id.has_value()) return 0;

        const auto send_result = cm_session.send_message(cauth::core::depot::make_pics_product_info_request(
            cauth::core::depot::PicsProductInfoRequest{
                {cauth::core::depot::PicsProductInfoAppRequest{*app_id, 0, true}},
                false,
                true,
            }));
        if (!send_result.ok) {
            err << "PICS product info request failed: " << send_result.error_message << '\n';
            return 1;
        }
        out << "PICS product info request sent for app " << *app_id << '\n';

        for (int attempt = 0; attempt < 8; ++attempt) {
            out << "Waiting for PICS product info response... poll " << (attempt + 1) << "/8\n";
            const auto received = cm_session.receive_messages(4);
            if (!received.ok) {
                err << "PICS product info receive failed: " << received.error_message << '\n';
                return 1;
            }
            for (const auto& message : received.messages) {
                if (message.emsg != cauth::core::cm::EMsg::ClientPICSProductInfoResponse) {
                    out << "CM message ignored while waiting for PICS: "
                        << cauth::core::cm::emsg_name(message.emsg)
                        << " (" << static_cast<std::uint32_t>(message.emsg) << ")\n";
                    continue;
                }
                const auto parsed = cauth::core::depot::parse_pics_product_info_response_body(message.body);
                if (!parsed.has_value()) {
                    err << "PICS product info response parse failed\n";
                    return 1;
                }
                out << "PICS app-info: apps=" << parsed->apps.size()
                    << " apps_unknown=" << parsed->apps_unknown
                    << " packages_unknown=" << parsed->packages_unknown
                    << " response_pending=" << (parsed->response_pending ? "true" : "false") << '\n';
                for (const auto& app : parsed->apps) {
                    out << "  app_id=" << app.app_id
                        << " change_number=" << app.change_number
                        << " buffer_bytes=" << app.buffer.size() << '\n';
                    if (app.buffer.empty()) continue;

                    cauth::core::depot::AppInfoParseDebug debug;
                    auto parsed_info = cauth::core::depot::parse_app_info_buffer(
                        app.buffer,
                        debug_app_info ? &debug : nullptr);
                    if (!parsed_info.has_value() && debug_app_info) {
                        out << "    appinfo debug: buffer_size=" << debug.buffer_size
                            << " best_offset=" << debug.best_offset
                            << " best_end_marker=" << static_cast<int>(debug.best_end_marker)
                            << " best_nodes=" << debug.best_nodes
                            << " best_score=" << debug.best_score << '\n'
                            << "    appinfo debug prefix: " << debug.prefix_hex << '\n';
                    }
                    if (!parsed_info.has_value()) {
                        out << "    appinfo: parse failed\n";
                        continue;
                    }
                    out << "    branches=" << parsed_info->branches.size()
                        << " depots=" << parsed_info->depots.size() << '\n';
                    for (const auto& branch : parsed_info->branches) {
                        out << "      branch=" << branch.name;
                        if (!branch.build_id.empty()) out << " buildid=" << branch.build_id;
                        if (branch.password_required) out << " password_required=true";
                        out << '\n';
                    }
                    for (const auto& depot : parsed_info->depots) {
                        out << "      depot=" << depot.depot_id
                            << " manifests=" << depot.manifests.size()
                            << " platform="
                            << cauth::core::depot::depot_platform_label(depot.os_list, depot.os_arch);
                        if (!depot.depot_from_app.empty()) {
                            out << " from_app=" << depot.depot_from_app;
                        }
                        if (depot.shared_install) {
                            out << " shared_install=true";
                        }
                        out << '\n';
                        for (const auto& manifest : depot.manifests) {
                            out << "        branch=" << manifest.branch
                                << " gid=" << manifest.manifest_gid;
                            if (manifest.encrypted) out << " encrypted=true";
                            out << '\n';
                        }
                    }
                }
                if (!parsed->response_pending) return 0;
            }
        }

        err << "PICS product info response not received\n";
        return 1;
    }

    err << "CM logon failed for all endpoints\n";
    return 1;
}

} // namespace

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

int run_cm_probe(const cauth::core::cm::CmServerQuery& query,
                 std::ostream& out,
                 std::ostream& err) {
    const auto result = load_servers(query, err);
    if (!result.has_value()) return 1;

    cauth::core::cm::CmWebSocketTransport transport;
    for (const auto& server : result->servers) {
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
                 std::ostream& out,
                 std::ostream& err) {
    return run_logon_or_app_info(query, steam_id, std::nullopt, false, out, err);
}

int run_cm_app_info(const cauth::core::cm::CmServerQuery& query,
                    std::uint64_t steam_id,
                    std::uint32_t app_id,
                    bool debug_app_info,
                    std::ostream& out,
                    std::ostream& err) {
    return run_logon_or_app_info(query, steam_id, app_id, debug_app_info, out, err);
}

} // namespace cauth::steam::auth
