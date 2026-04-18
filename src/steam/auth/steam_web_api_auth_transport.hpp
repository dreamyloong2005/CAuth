#ifndef CAUTH_CORE_AUTH_STEAM_WEB_API_AUTH_TRANSPORT_HPP
#define CAUTH_CORE_AUTH_STEAM_WEB_API_AUTH_TRANSPORT_HPP

#include "steam/auth/steam_auth_transport.hpp"

namespace cauth::steam::auth {

class SteamWebApiAuthenticationTransport final : public SteamAuthenticationTransport {
  public:
    SteamTransportResponse<SteamRsaPublicKey>
    get_password_rsa_public_key(const std::string& account_name) override;

    SteamTransportResponse<SteamBeginAuthSessionResponse>
    begin_auth_session_via_credentials(const SteamBeginAuthSessionRequest& request) override;

    SteamTransportResponse<SteamPollAuthSessionStatusResponse>
    poll_auth_session_status(const SteamPollAuthSessionStatusRequest& request) override;

    SteamTransportResponse<SteamGenerateAccessTokenForAppResponse>
    generate_access_token_for_app(const SteamGenerateAccessTokenForAppRequest& request) override;
};

std::string steam_web_api_url_encode(std::string_view value);
std::vector<std::uint8_t> encode_begin_auth_session_request(
    const SteamBeginAuthSessionRequest& request);
std::vector<std::uint8_t> encode_poll_auth_session_status_request(
    const SteamPollAuthSessionStatusRequest& request);
std::vector<std::uint8_t> encode_generate_access_token_for_app_request(
    const SteamGenerateAccessTokenForAppRequest& request);
std::string steam_login_platform_website_id(SteamLoginPlatformType platform_type);
std::optional<SteamRsaPublicKey> parse_get_password_rsa_public_key_response(
    std::string_view json);
std::optional<SteamBeginAuthSessionResponse> parse_begin_auth_session_response(
    std::string_view json);
std::optional<SteamPollAuthSessionStatusResponse> parse_poll_auth_session_status_response(
    std::string_view bytes);
std::optional<SteamGenerateAccessTokenForAppResponse>
parse_generate_access_token_for_app_response(std::string_view bytes);

} // namespace cauth::steam::auth

#endif
