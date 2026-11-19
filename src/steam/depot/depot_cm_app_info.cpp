#include "steam/depot/depot_cm_app_info.hpp"

#include "steam/auth/steam_auth_cm_application.hpp"
#include "steam/cm/cm_message.hpp"
#include "steam/cm/cm_session.hpp"
#include "steam/depot/app_info.hpp"
#include "steam/depot/pics.hpp"

#include <ostream>

namespace cauth::steam::depot {
namespace {

int request_app_info(cauth::core::cm::CmSession& cm_session,
                     std::uint32_t app_id,
                     bool debug_app_info,
                     std::ostream& out,
                     std::ostream& err) {
    const auto send_result = cm_session.send_message(cauth::core::depot::make_pics_product_info_request(
        cauth::core::depot::PicsProductInfoRequest{
            {cauth::core::depot::PicsProductInfoAppRequest{app_id, 0, true}},
            false,
            true,
        }));
    if (!send_result.ok) {
        err << "PICS product info request failed: " << send_result.error_message << '\n';
        return 1;
    }
    out << "PICS product info request sent for app " << app_id << '\n';

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

} // namespace

int run_cm_app_info(const cauth::core::cm::CmServerQuery& query,
                    std::uint64_t steam_id,
                    std::uint32_t app_id,
                    bool debug_app_info,
                    const cauth::core::platform::RouteSelection* route_selection,
                    std::ostream& out,
                    std::ostream& err) {
    return cauth::steam::auth::run_cm_logon_then(
        query,
        steam_id,
        route_selection,
        [app_id, debug_app_info](cauth::core::cm::CmSession& session, std::ostream& session_out,
                                 std::ostream& session_err) {
            return request_app_info(session, app_id, debug_app_info, session_out, session_err);
        },
        out,
        err);
}

} // namespace cauth::steam::depot
