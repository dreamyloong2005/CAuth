#ifndef CAUTH_STEAM_AUTH_LOGIN_TYPES_HPP
#define CAUTH_STEAM_AUTH_LOGIN_TYPES_HPP

#include "core/auth/auth_result.hpp"
#include "core/session/auth_session.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <optional>
#include <string>

namespace cauth::steam::auth {

enum class SteamLoginPlatformType {
    SteamClient,
    WebBrowser,
    MobileApp,
};

struct SteamLoginRequest {
    std::string account_name;
    std::string password;
    std::string steam_guard_code;
    std::string device_name = "CAuth";
    bool remember_session = true;
    SteamLoginPlatformType platform_type = SteamLoginPlatformType::SteamClient;
};

enum class SteamLoginStatus {
    Succeeded,
    SteamGuardRequired,
    Failed,
    Unsupported,
};

struct SteamLoginResult {
    SteamLoginStatus status = SteamLoginStatus::Failed;
    std::optional<cauth::core::session::AuthSession> session;
    std::string message;
};

inline cauth::core::auth::AuthStatus to_core_auth_status(SteamLoginStatus status) noexcept {
    switch (status) {
    case SteamLoginStatus::Succeeded:
        return cauth::core::auth::AuthStatus::Succeeded;
    case SteamLoginStatus::SteamGuardRequired:
        return cauth::core::auth::AuthStatus::AdditionalVerificationRequired;
    case SteamLoginStatus::Failed:
        return cauth::core::auth::AuthStatus::Failed;
    case SteamLoginStatus::Unsupported:
        return cauth::core::auth::AuthStatus::Unsupported;
    }
    return cauth::core::auth::AuthStatus::Failed;
}

inline cauth::core::auth::AuthResult to_core_auth_result(const SteamLoginResult& result) {
    cauth::core::auth::AuthResult auth_result;
    auth_result.status = to_core_auth_status(result.status);
    auth_result.challenge_kind = result.status == SteamLoginStatus::SteamGuardRequired
                                     ? cauth::core::auth::AuthChallengeKind::Verification
                                     : cauth::core::auth::AuthChallengeKind::None;
    auth_result.session = result.session;
    auth_result.provider = result.session.has_value() && !result.session->provider.empty()
                               ? result.session->provider
                               : std::string{cauth::steam::auth::kSteamAuthProvider};
    auth_result.message = result.message;
    return auth_result;
}

} // namespace cauth::steam::auth

#endif
