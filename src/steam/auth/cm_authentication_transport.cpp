#include "steam/auth/cm_authentication_transport.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cauth::steam::auth {
namespace {

constexpr auto kBeginAuthSessionViaCredentials =
    "Authentication.BeginAuthSessionViaCredentials#1";
constexpr auto kPollAuthSessionStatus = "Authentication.PollAuthSessionStatus#1";

bool steam_auth_debug_enabled() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t value_length = 0;
    if (_dupenv_s(&value, &value_length, "CAUTH_DEBUG_STEAM_AUTH") != 0 || value == nullptr) {
        return false;
    }

    const std::string debug_value{value};
    std::free(value);
    return value_length > 0 && debug_value == "1";
#else
    const auto* value = std::getenv("CAUTH_DEBUG_STEAM_AUTH");
    return value != nullptr && std::string_view{value} == "1";
#endif
}

template <typename T>
SteamTransportResponse<T> transport_error(std::string error_message) {
    return {{false, std::move(error_message)}, std::nullopt};
}

} // namespace

CmAuthenticationTransport::CmAuthenticationTransport(cauth::core::cm::CmServiceMethodClient& service_client)
    : service_client_(service_client) {}

std::uint64_t CmAuthenticationTransport::next_job_id() {
    return next_job_id_++;
}

SteamTransportResponse<SteamRsaPublicKey>
CmAuthenticationTransport::get_password_rsa_public_key(const std::string& account_name) {
    return web_api_fallback_.get_password_rsa_public_key(account_name);
}

SteamTransportResponse<SteamBeginAuthSessionResponse>
CmAuthenticationTransport::begin_auth_session_via_credentials(
    const SteamBeginAuthSessionRequest& request) {
    const auto body = encode_begin_auth_session_request(request);
    const auto result = service_client_.call_non_authed(kBeginAuthSessionViaCredentials, body,
                                                        next_job_id());
    if (!result.ok) {
        return transport_error<SteamBeginAuthSessionResponse>(result.error_message);
    }

    const auto parsed =
        parse_begin_auth_session_response(std::string_view{
            reinterpret_cast<const char*>(result.body.data()), result.body.size()});
    if (!parsed.has_value()) {
        return transport_error<SteamBeginAuthSessionResponse>(
            "failed to parse CM BeginAuthSessionViaCredentials response");
    }

    return {{true, ""}, *parsed};
}

SteamTransportResponse<SteamPollAuthSessionStatusResponse>
CmAuthenticationTransport::poll_auth_session_status(
    const SteamPollAuthSessionStatusRequest& request) {
    const auto body = encode_poll_auth_session_status_request(request);
    const auto result =
        service_client_.call_non_authed(kPollAuthSessionStatus, body, next_job_id());
    if (!result.ok) {
        return transport_error<SteamPollAuthSessionStatusResponse>(result.error_message);
    }

    const auto parsed =
        parse_poll_auth_session_status_response(std::string_view{
            reinterpret_cast<const char*>(result.body.data()), result.body.size()});
    if (!parsed.has_value()) {
        return transport_error<SteamPollAuthSessionStatusResponse>(
            "failed to parse CM PollAuthSessionStatus response");
    }

    if (steam_auth_debug_enabled()) {
        std::cerr << "CM PollAuthSessionStatus parsed:"
                  << " refresh_token_len=" << parsed->refresh_token.size()
                  << " access_token_len=" << parsed->access_token.size()
                  << " account_name=" << parsed->account_name
                  << " had_remote_interaction="
                  << (parsed->had_remote_interaction ? "true" : "false") << '\n';
    }

    return {{true, ""}, *parsed};
}

SteamTransportResponse<SteamGenerateAccessTokenForAppResponse>
CmAuthenticationTransport::generate_access_token_for_app(
    const SteamGenerateAccessTokenForAppRequest&) {
    return transport_error<SteamGenerateAccessTokenForAppResponse>(
        "GenerateAccessTokenForApp over CM is not implemented yet");
}

} // namespace cauth::steam::auth
