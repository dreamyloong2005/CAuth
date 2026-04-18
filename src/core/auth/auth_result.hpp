#ifndef CAUTH_CORE_AUTH_AUTH_RESULT_HPP
#define CAUTH_CORE_AUTH_AUTH_RESULT_HPP

#include "core/session/auth_session.hpp"

#include <optional>
#include <string>

namespace cauth::core::auth {

enum class AuthStatus {
    Succeeded,
    AdditionalVerificationRequired,
    Failed,
    Unsupported,
};

enum class AuthChallengeKind {
    None,
    Verification,
};

struct AuthResult {
    AuthStatus status = AuthStatus::Failed;
    AuthChallengeKind challenge_kind = AuthChallengeKind::None;
    std::optional<session::AuthSession> session;
    std::string provider;
    std::string message;
};

} // namespace cauth::core::auth

#endif
