#include "steam/depot/depot_cm_client.hpp"

#include "steam/cm/cm_session.hpp"
#include "steam/cm/steam_cm_connector.hpp"
#include "steam/depot/pics.hpp"

#include <functional>
#include <ostream>

namespace cauth::core::depot {
namespace {

void write_line(std::ostream* stream, const std::string& message) {
    if (stream != nullptr) {
        *stream << message << '\n';
    }
}

using SessionOperation = std::function<cm::SteamCmAttemptResult(cm::CmSession&)>;

bool with_authed_cm_session(const cauth::steam::auth::SteamAuthProvider& auth_provider,
                            std::uint32_t max_count,
                            std::ostream* out,
                            std::ostream* err,
                            const SessionOperation& operation) {
    const auto loaded = auth_provider.load_auth_session();
    if (!loaded.ok || !loaded.session.has_value()) {
        write_line(err, loaded.error_message.empty() ? "Auth session: unavailable"
                                                     : loaded.error_message);
        return false;
    }
    cm::SteamCmConnector connector{out, err};
    const auto result =
        connector.with_logged_on_session(*loaded.session, max_count,
                                         [&](const cm::CmServerEndpoint&, cm::CmSession& cm_session) {
                                             return operation(cm_session);
                                         });
    if (!result.ok && !result.error_message.empty()) {
        write_line(err, result.error_message);
    }
    return result.ok;
}

} // namespace

DepotCmClient::DepotCmClient(cauth::steam::auth::SteamAuthProvider& auth_provider,
                             std::ostream* out,
                             std::ostream* err)
    : auth_provider_(&auth_provider), out_(out), err_(err) {}

std::optional<AppInfo> DepotCmClient::fetch_app_info(std::uint32_t app_id,
                                                     std::uint32_t max_count) const {
    std::optional<AppInfo> app_info;
    const auto ok = with_authed_cm_session(
        *auth_provider_, max_count, out_, err_,
        [&](cm::CmSession& cm_session) {
            const auto send_result = cm_session.send_message(make_pics_product_info_request(
                PicsProductInfoRequest{{PicsProductInfoAppRequest{app_id, 0, true}}, false, true}));
            if (!send_result.ok) {
                write_line(err_, "PICS product info request failed: " + send_result.error_message);
                return cm::SteamCmAttemptResult{
                    cm::SteamCmContinuation::Stop, false,
                    "PICS product info request failed: " + send_result.error_message};
            }

            for (int attempt = 0; attempt < 8; ++attempt) {
                const auto received = cm_session.receive_messages(4);
                if (!received.ok) {
                    write_line(err_, "PICS product info receive failed: " + received.error_message);
                    return cm::SteamCmAttemptResult{
                        cm::SteamCmContinuation::Stop, false,
                        "PICS product info receive failed: " + received.error_message};
                }

                for (const auto& message : received.messages) {
                    if (message.emsg != cm::EMsg::ClientPICSProductInfoResponse) {
                        continue;
                    }

                    const auto parsed = parse_pics_product_info_response_body(message.body);
                    if (!parsed.has_value()) {
                        write_line(err_, "PICS product info response parse failed");
                        return cm::SteamCmAttemptResult{
                            cm::SteamCmContinuation::Stop, false,
                            "PICS product info response parse failed"};
                    }

                    for (const auto& app : parsed->apps) {
                        if (app.app_id != app_id || app.buffer.empty()) {
                            continue;
                        }

                        app_info = parse_app_info_buffer(app.buffer);
                        if (!app_info.has_value()) {
                            write_line(err_, "appinfo parse failed");
                            return cm::SteamCmAttemptResult{
                                cm::SteamCmContinuation::Stop, false, "appinfo parse failed"};
                        }
                        return cm::SteamCmAttemptResult{
                            cm::SteamCmContinuation::Stop, true, ""};
                    }
                }
            }

            write_line(err_, "PICS product info response not received");
            return cm::SteamCmAttemptResult{
                cm::SteamCmContinuation::Stop, false, "PICS product info response not received"};
        });
    return ok ? app_info : std::nullopt;
}

std::optional<DepotDecryptionKeyResponse> DepotCmClient::fetch_depot_key(std::uint32_t app_id,
                                                                         std::uint32_t depot_id,
                                                                         std::uint32_t max_count) const {
    std::optional<DepotDecryptionKeyResponse> response;
    const auto ok = with_authed_cm_session(
        *auth_provider_, max_count, out_, err_,
        [&](cm::CmSession& cm_session) {
            const auto send_result = cm_session.send_message(
                make_depot_decryption_key_request(DepotDecryptionKeyRequest{depot_id, app_id}));
            if (!send_result.ok) {
                write_line(err_, "Depot key request failed: " + send_result.error_message);
                return cm::SteamCmAttemptResult{
                    cm::SteamCmContinuation::Stop, false,
                    "Depot key request failed: " + send_result.error_message};
            }

            for (int attempt = 0; attempt < 8; ++attempt) {
                const auto received = cm_session.receive_messages(4);
                if (!received.ok) {
                    write_line(err_, "Depot key receive failed: " + received.error_message);
                    return cm::SteamCmAttemptResult{
                        cm::SteamCmContinuation::Stop, false,
                        "Depot key receive failed: " + received.error_message};
                }

                for (const auto& message : received.messages) {
                    if (message.emsg != cm::EMsg::ClientGetDepotDecryptionKeyResponse) {
                        continue;
                    }

                    response = parse_depot_decryption_key_response_body(message.body);
                    if (!response.has_value()) {
                        write_line(err_, "Depot key response parse failed");
                        return cm::SteamCmAttemptResult{
                            cm::SteamCmContinuation::Stop, false,
                            "Depot key response parse failed"};
                    }
                    return cm::SteamCmAttemptResult{
                        cm::SteamCmContinuation::Stop, true, ""};
                }
            }

            write_line(err_, "Depot key response not received");
            return cm::SteamCmAttemptResult{
                cm::SteamCmContinuation::Stop, false, "Depot key response not received"};
        });
    return ok ? response : std::nullopt;
}

std::optional<ManifestRequestCodeResponse> DepotCmClient::fetch_manifest_request_code(
    const ManifestRequestCodeRequest& request,
    std::uint32_t max_count) const {
    std::optional<ManifestRequestCodeResponse> response;
    const auto ok = with_authed_cm_session(
        *auth_provider_, max_count, out_, err_,
        [&](cm::CmSession& cm_session) {
            const auto call = cm_session.call_service_method(
                kGetManifestRequestCodeMethod, encode_manifest_request_code_request_body(request),
                0x63617574686d616eULL);
            if (!call.ok) {
                write_line(err_, "Manifest request-code failed: " + call.error_message);
                return cm::SteamCmAttemptResult{
                    cm::SteamCmContinuation::Stop, false,
                    "Manifest request-code failed: " + call.error_message};
            }
            if (call.header.eresult != 0 && call.header.eresult != 1) {
                std::string message = "Manifest request-code rejected with eresult " +
                                      std::to_string(call.header.eresult);
                if (!call.header.error_message.empty()) {
                    message += ": " + call.header.error_message;
                }
                write_line(err_, message);
                return cm::SteamCmAttemptResult{
                    cm::SteamCmContinuation::Stop, false, std::move(message)};
            }

            response = parse_manifest_request_code_response_body(call.body);
            if (!response.has_value()) {
                write_line(err_, "Manifest request-code response parse failed");
                return cm::SteamCmAttemptResult{
                    cm::SteamCmContinuation::Stop, false,
                    "Manifest request-code response parse failed"};
            }
            return cm::SteamCmAttemptResult{
                cm::SteamCmContinuation::Stop, true, ""};
        });
    return ok ? response : std::nullopt;
}

} // namespace cauth::core::depot
