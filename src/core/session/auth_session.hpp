#ifndef CAUTH_CORE_SESSION_AUTH_SESSION_HPP
#define CAUTH_CORE_SESSION_AUTH_SESSION_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace cauth::core::session {

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

bool is_valid(const AuthSession& session) noexcept;
std::string redacted_account_label(const AuthSession& session);
void set_provider(AuthSession& session, std::string provider);
void set_subject_id(AuthSession& session, std::string subject_id);
std::optional<std::uint64_t> parse_numeric_subject_id(const AuthSession& session) noexcept;

} // namespace cauth::core::session

#endif
