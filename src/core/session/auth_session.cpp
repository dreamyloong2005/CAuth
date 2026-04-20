#include "core/session/auth_session.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

namespace cauth::core::session {
namespace {

bool same_account(const AuthSession& session, const AuthSessionKey& key) noexcept {
    return matches_session(session, key);
}

bool same_session_slot(const AuthSession& left, const AuthSession& right) noexcept {
    return left.provider == right.provider &&
           left.subject_id == right.subject_id &&
           left.session_type == right.session_type;
}

} // namespace

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

bool is_valid(const AuthSessionKey& key) noexcept {
    return !key.provider.empty() && !key.subject_id.empty();
}

bool is_valid(const AuthSession& session) noexcept {
    return !session.provider.empty() && !session.subject_id.empty() &&
           !session.refresh_token.empty();
}

AuthSessionKey session_key(const AuthSession& session) {
    return AuthSessionKey{session.provider, session.subject_id};
}

bool matches_session(const AuthSession& session, const AuthSessionKey& key) noexcept {
    return matches_session(session, key.provider, key.subject_id);
}

bool matches_session(const AuthSession& session,
                     std::string_view provider,
                     std::string_view subject_id) noexcept {
    return session.provider == provider && session.subject_id == subject_id;
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

std::optional<AuthSession> find_auth_session(const AuthSessionRepositoryState& state,
                                             const AuthSessionKey& key) {
    std::optional<AuthSession> selected;
    for (const auto& session : state.sessions) {
        if (!same_account(session, key)) {
            continue;
        }
        if (!selected.has_value() || session.created_at >= selected->created_at) {
            selected = session;
        }
    }
    return selected;
}

void upsert_auth_session(AuthSessionRepositoryState& state, const AuthSession& session) {
    if (!is_valid(session)) {
        return;
    }

    const auto found = std::find_if(
        state.sessions.begin(),
        state.sessions.end(),
        [&](const AuthSession& candidate) { return same_session_slot(candidate, session); });
    if (found == state.sessions.end()) {
        state.sessions.push_back(session);
    } else {
        *found = session;
    }
}

bool remove_auth_session(AuthSessionRepositoryState& state, const AuthSessionKey& key) {
    const auto before = state.sessions.size();
    state.sessions.erase(
        std::remove_if(
            state.sessions.begin(),
            state.sessions.end(),
            [&](const AuthSession& candidate) { return same_account(candidate, key); }),
        state.sessions.end());
    if (state.sessions.size() == before) {
        return false;
    }

    normalize_auth_session_repository_state(state);
    return true;
}

void normalize_auth_session_repository_state(AuthSessionRepositoryState& state) {
    AuthSessionRepositoryState normalized;
    for (const auto& session : state.sessions) {
        upsert_auth_session(normalized, session);
    }

    state = std::move(normalized);
}

} // namespace cauth::core::session
