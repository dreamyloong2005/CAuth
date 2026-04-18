#include "steam/auth/placeholder_steam_authenticator.hpp"

namespace cauth::steam::auth {

SteamLoginResult PlaceholderSteamAuthenticator::login(const SteamLoginRequest&) {
    return SteamLoginResult{
        SteamLoginStatus::Unsupported,
        std::nullopt,
        "Steam network login adapter is not implemented yet",
    };
}

} // namespace cauth::steam::auth
