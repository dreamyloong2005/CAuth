#include "core/runtime/session/memory_session_repository.hpp"

namespace cauth::core::runtime {

void MemorySessionRepository::save_auth_session(const session::AuthSession& session) {
    session_ = session;
}

std::optional<session::AuthSession> MemorySessionRepository::load_auth_session() const {
    return session_;
}

void MemorySessionRepository::clear_auth_session() {
    session_.reset();
}

} // namespace cauth::core::runtime
