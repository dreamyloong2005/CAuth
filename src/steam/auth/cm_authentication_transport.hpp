#ifndef CAUTH_CORE_AUTH_CM_AUTHENTICATION_TRANSPORT_HPP
#define CAUTH_CORE_AUTH_CM_AUTHENTICATION_TRANSPORT_HPP

#include "steam/auth/steam_auth_transport.hpp"
#include "steam/auth/steam_web_api_auth_transport.hpp"
#include "steam/cm/cm_service_method.hpp"

#include <cstdint>

namespace cauth::steam::auth {

class CmAuthenticationTransport final : public SteamAuthenticationTransport {
  public:
    explicit CmAuthenticationTransport(cauth::core::cm::CmServiceMethodClient& service_client);

    SteamTransportResponse<SteamRsaPublicKey>
    get_password_rsa_public_key(const std::string& account_name) override;

    SteamTransportResponse<SteamBeginAuthSessionResponse>
    begin_auth_session_via_credentials(const SteamBeginAuthSessionRequest& request) override;

    SteamTransportResponse<SteamPollAuthSessionStatusResponse>
    poll_auth_session_status(const SteamPollAuthSessionStatusRequest& request) override;

    SteamTransportResponse<SteamGenerateAccessTokenForAppResponse>
    generate_access_token_for_app(const SteamGenerateAccessTokenForAppRequest& request) override;

  private:
    std::uint64_t next_job_id();

    cauth::core::cm::CmServiceMethodClient& service_client_;
    SteamWebApiAuthenticationTransport web_api_fallback_;
    std::uint64_t next_job_id_ = 1;
};

} // namespace cauth::steam::auth

#endif
