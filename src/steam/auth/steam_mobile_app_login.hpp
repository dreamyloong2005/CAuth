#ifndef CAUTH_STEAM_AUTH_STEAM_MOBILE_APP_LOGIN_HPP
#define CAUTH_STEAM_AUTH_STEAM_MOBILE_APP_LOGIN_HPP

#include "steam/auth/steam_auth_transport.hpp"
#include "steam/auth/steam_authenticator.hpp"

namespace cauth::steam::auth {

class SteamMobileAppLogin final : public SteamAuthenticator {
  public:
    SteamMobileAppLogin(SteamAuthenticator& inner, SteamAuthenticationTransport& transport);

    SteamLoginResult login(const SteamLoginRequest& request) override;

  private:
    SteamAuthenticator* inner_;
    SteamAuthenticationTransport* transport_;
};

} // namespace cauth::steam::auth

#endif
