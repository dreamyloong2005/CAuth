#ifndef CAUTH_STEAM_AUTH_STEAM_SESSION_IDENTITY_HPP
#define CAUTH_STEAM_AUTH_STEAM_SESSION_IDENTITY_HPP

#include "core/session/auth_session.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace cauth::steam::auth {

inline constexpr std::string_view kSteamAuthProvider = "steam";
inline constexpr std::string_view kSteamSessionTypeSteamClient = "steam-client";
inline constexpr std::string_view kSteamSessionTypeWebBrowser = "web-browser";
inline constexpr std::string_view kSteamSessionTypeMobileApp = "mobile-app";

inline bool is_steam_session(const cauth::core::session::AuthSession& session) noexcept {
    return session.provider == kSteamAuthProvider;
}

inline std::uint64_t steam_id(const cauth::core::session::AuthSession& session) noexcept {
    if (!is_steam_session(session)) {
        return 0;
    }

    return cauth::core::session::parse_numeric_subject_id(session).value_or(0);
}

inline void set_steam_id(cauth::core::session::AuthSession& session,
                         std::uint64_t steam_id_value) {
    cauth::core::session::set_provider(session, std::string{kSteamAuthProvider});
    cauth::core::session::set_subject_id(session, std::to_string(steam_id_value));
}

} // namespace cauth::steam::auth

#endif
