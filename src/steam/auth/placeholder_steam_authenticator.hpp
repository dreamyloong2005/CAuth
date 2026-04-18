#ifndef CAUTH_CORE_AUTH_PLACEHOLDER_STEAM_AUTHENTICATOR_HPP
#define CAUTH_CORE_AUTH_PLACEHOLDER_STEAM_AUTHENTICATOR_HPP

#include "steam/auth/steam_authenticator.hpp"

namespace cauth::steam::auth {

class PlaceholderSteamAuthenticator final : public SteamAuthenticator {
  public:
    SteamLoginResult login(const SteamLoginRequest& request) override;
};

} // namespace cauth::steam::auth

#endif
