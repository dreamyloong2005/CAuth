#include "core/runtime/session/memory_session_repository.hpp"

namespace cauth::core::runtime {

void MemorySessionRepository::save_auth_session(const session::AuthSession& session) {
    session::upsert_auth_session(state_, session);
}

std::optional<session::AuthSession> MemorySessionRepository::load_auth_session() const {
    return session::active_auth_session(state_);
}

void MemorySessionRepository::clear_auth_session() {
    if (!state_.active.has_value()) {
        return;
    }
    session::remove_auth_session(state_, *state_.active);
}

std::vector<session::AuthSession> MemorySessionRepository::list_auth_sessions() const {
    return state_.sessions;
}

std::optional<session::AuthSession> MemorySessionRepository::load_auth_session(
    std::string_view provider,
    std::string_view subject_id) const {
    return session::find_auth_session(
        state_,
        session::AuthSessionKey{std::string{provider}, std::string{subject_id}});
}

std::optional<session::AuthSessionKey> MemorySessionRepository::active_auth_session_key() const {
    return state_.active;
}

bool MemorySessionRepository::set_active_auth_session(std::string_view provider,
                                                      std::string_view subject_id) {
    return session::set_active_auth_session(
        state_,
        session::AuthSessionKey{std::string{provider}, std::string{subject_id}});
}

void MemorySessionRepository::clear_auth_session(std::string_view provider,
                                                 std::string_view subject_id) {
    session::remove_auth_session(
        state_,
        session::AuthSessionKey{std::string{provider}, std::string{subject_id}});
}

void MemorySessionRepository::clear_all_auth_sessions() {
    state_ = {};
}

} // namespace cauth::core::runtime
