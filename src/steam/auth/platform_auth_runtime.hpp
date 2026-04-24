#ifndef CAUTH_STEAM_AUTH_PLATFORM_AUTH_RUNTIME_HPP
#define CAUTH_STEAM_AUTH_PLATFORM_AUTH_RUNTIME_HPP

#include "steam/auth/steam_authenticator.hpp"
#include "steam/auth/steam_network_authenticator.hpp"
#include "steam/auth/steam_auth_transport.hpp"
#include "steam/auth/steam_password_encryptor.hpp"
#include "core/platform/route_selection.hpp"
#include "core/session/auth_session_storage.hpp"

#include <memory>

namespace cauth::steam::auth {

struct PlatformAuthRuntime {
    std::unique_ptr<SteamAuthenticationTransport> transport;
    std::unique_ptr<SteamPasswordEncryptor> password_encryptor;
    std::unique_ptr<SteamAuthenticator> authenticator;
};

struct SteamPlatformLoginOptions {
    SteamNetworkAuthenticatorOptions authenticator_options;
    std::uint32_t cm_max_count = 5;
    cauth::core::platform::RouteSelection cm_route_selection;
};

PlatformAuthRuntime make_steam_platform_auth_runtime();
SteamLoginResult login_with_steam_platform_auth(
    cauth::core::session::AuthSessionWriter& session_writer,
    const SteamLoginRequest& request,
    const SteamPlatformLoginOptions& options);
SteamLoginResult login_with_steam_platform_auth(
    cauth::core::session::AuthSessionWriter& session_writer,
    const SteamLoginRequest& request);

} // namespace cauth::steam::auth

#endif
