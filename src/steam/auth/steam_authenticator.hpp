#ifndef CAUTH_CORE_AUTH_STEAM_AUTHENTICATOR_HPP
#define CAUTH_CORE_AUTH_STEAM_AUTHENTICATOR_HPP

#include "steam/auth/login_types.hpp"

namespace cauth::steam::auth {

class SteamAuthenticator {
  public:
    virtual ~SteamAuthenticator() = default;

    virtual SteamLoginResult login(const SteamLoginRequest& request) = 0;
};

} // namespace cauth::steam::auth

#endif
