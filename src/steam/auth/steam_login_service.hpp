#ifndef CAUTH_STEAM_AUTH_STEAM_LOGIN_SERVICE_HPP
#define CAUTH_STEAM_AUTH_STEAM_LOGIN_SERVICE_HPP

#include "steam/auth/steam_authenticator.hpp"
#include "core/session/auth_session_storage.hpp"

namespace cauth::steam::auth {

class SteamLoginService {
  public:
    SteamLoginService(SteamAuthenticator& authenticator,
                      cauth::core::session::AuthSessionWriter& session_writer);

    SteamLoginResult login(const SteamLoginRequest& request);

  private:
    SteamAuthenticator* authenticator_;
    cauth::core::session::AuthSessionWriter* session_writer_;
};

} // namespace cauth::steam::auth

#endif
