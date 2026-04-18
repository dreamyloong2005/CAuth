#ifndef CAUTH_CORE_AUTH_UNIMPLEMENTED_STEAM_AUTH_TRANSPORT_HPP
#define CAUTH_CORE_AUTH_UNIMPLEMENTED_STEAM_AUTH_TRANSPORT_HPP

#include "steam/auth/steam_auth_transport.hpp"

namespace cauth::steam::auth {

class UnimplementedSteamAuthenticationTransport final : public SteamAuthenticationTransport {
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

} // namespace cauth::steam::auth

#endif
