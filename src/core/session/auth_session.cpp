#include "core/session/auth_session.hpp"

#include <charconv>
#include <utility>

namespace cauth::core::session {

AuthSession::AuthSession(std::string provider_value,
                         std::string subject_id_value,
                         std::string account_name_value,
                         std::string refresh_token_value,
                         std::string access_token_value,
                         std::string session_type_value,
                         std::chrono::system_clock::time_point created_at_value)
    : account_name(std::move(account_name_value)),
      refresh_token(std::move(refresh_token_value)),
      access_token(std::move(access_token_value)),
      session_type(std::move(session_type_value)),
      created_at(created_at_value),
      provider(std::move(provider_value)),
      subject_id(std::move(subject_id_value)) {}

bool is_valid(const AuthSession& session) noexcept {
    return !session.provider.empty() && !session.subject_id.empty() &&
           !session.account_name.empty() && !session.refresh_token.empty();
}

std::string redacted_account_label(const AuthSession& session) {
    if (session.account_name.empty()) {
        return "<unknown>";
    }

    if (session.account_name.size() <= 2) {
        return "**";
    }

    return session.account_name.substr(0, 1) + "***" +
           session.account_name.substr(session.account_name.size() - 1);
}

void set_provider(AuthSession& session, std::string provider) {
    session.provider = std::move(provider);
}

void set_subject_id(AuthSession& session, std::string subject_id) {
    session.subject_id = std::move(subject_id);
}

std::optional<std::uint64_t> parse_numeric_subject_id(const AuthSession& session) noexcept {
    if (session.subject_id.empty()) {
        return std::nullopt;
    }

    std::uint64_t value = 0;
    const auto* begin = session.subject_id.data();
    const auto* end = begin + session.subject_id.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }

    return value;
}

} // namespace cauth::core::session
