#ifndef CAUTH_CORE_SESSION_AUTH_SESSION_HPP
#define CAUTH_CORE_SESSION_AUTH_SESSION_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::core::session {

struct AuthSessionKey {
    std::string provider;
    std::string subject_id;
};

struct AuthSession {
    std::string account_name;
    std::string refresh_token;
    std::string access_token;
    std::string session_type;
    std::chrono::system_clock::time_point created_at = std::chrono::system_clock::now();
    std::string provider;
    std::string subject_id;

    AuthSession() = default;

    AuthSession(std::string provider_value,
                std::string subject_id_value,
                std::string account_name_value,
                std::string refresh_token_value,
                std::string access_token_value = {},
                std::string session_type_value = {},
                std::chrono::system_clock::time_point created_at_value =
                    std::chrono::system_clock::now());
};

struct AuthSessionRepositoryState {
    std::vector<AuthSession> sessions;
};

bool is_valid(const AuthSessionKey& key) noexcept;
bool is_valid(const AuthSession& session) noexcept;
AuthSessionKey session_key(const AuthSession& session);
bool matches_session(const AuthSession& session, const AuthSessionKey& key) noexcept;
bool matches_session(const AuthSession& session,
                     std::string_view provider,
                     std::string_view subject_id) noexcept;
std::string redacted_account_label(const AuthSession& session);
void set_provider(AuthSession& session, std::string provider);
void set_subject_id(AuthSession& session, std::string subject_id);
std::optional<std::uint64_t> parse_numeric_subject_id(const AuthSession& session) noexcept;
std::optional<AuthSession> find_auth_session(const AuthSessionRepositoryState& state,
                                             const AuthSessionKey& key);
void upsert_auth_session(AuthSessionRepositoryState& state, const AuthSession& session);
bool remove_auth_session(AuthSessionRepositoryState& state, const AuthSessionKey& key);
void normalize_auth_session_repository_state(AuthSessionRepositoryState& state);

} // namespace cauth::core::session

#endif
