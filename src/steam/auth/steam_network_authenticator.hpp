#ifndef CAUTH_CORE_AUTH_STEAM_NETWORK_AUTHENTICATOR_HPP
#define CAUTH_CORE_AUTH_STEAM_NETWORK_AUTHENTICATOR_HPP

#include "steam/auth/steam_authenticator.hpp"
#include "steam/auth/steam_auth_transport.hpp"
#include "steam/auth/steam_password_encryptor.hpp"

#include <functional>

namespace cauth::steam::auth {

struct SteamNetworkAuthenticatorOptions {
    int max_poll_attempts = 24;
    std::function<void(int attempt, int max_attempts, double interval_seconds)> on_poll_waiting;
    std::function<bool()> cancel_requested;
};

class SteamNetworkAuthenticator final : public SteamAuthenticator {
  public:
    SteamNetworkAuthenticator(SteamAuthenticationTransport& transport,
                              SteamPasswordEncryptor& password_encryptor,
                              SteamNetworkAuthenticatorOptions options = {});

    SteamLoginResult login(const SteamLoginRequest& request) override;

  private:
    SteamAuthenticationTransport* transport_;
    SteamPasswordEncryptor* password_encryptor_;
    SteamNetworkAuthenticatorOptions options_;
};

} // namespace cauth::steam::auth

#endif
