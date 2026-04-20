#include "core/runtime/session/android_session_repository.hpp"

#include "core/session/auth_session_codec.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace cauth::core::runtime {

AndroidSessionRepository::AndroidSessionRepository(AndroidSecureStorageBridge& bridge)
    : bridge_(&bridge) {}

void AndroidSessionRepository::save_auth_session(const session::AuthSession& session) {
    auto state = load_repository_state();
    session::upsert_auth_session(state, session);
    save_repository_state(state);
}

std::vector<session::AuthSession> AndroidSessionRepository::list_auth_sessions() const {
    return load_repository_state().sessions;
}

std::optional<session::AuthSession> AndroidSessionRepository::load_auth_session(std::string_view provider,
                                                                                std::string_view subject_id) const {
    return session::find_auth_session(
        load_repository_state(),
        session::AuthSessionKey{std::string(provider), std::string(subject_id)});
}

void AndroidSessionRepository::clear_auth_session(std::string_view provider,
                                                  std::string_view subject_id) {
    auto state = load_repository_state();
    if (session::remove_auth_session(
            state,
            session::AuthSessionKey{std::string(provider), std::string(subject_id)})) {
        save_repository_state(state);
    }
}

void AndroidSessionRepository::clear_all_auth_sessions() {
    bridge_->clear_bytes();
}

session::AuthSessionRepositoryState AndroidSessionRepository::load_repository_state() const {
    const auto bytes = bridge_->load_bytes();
    if (!bytes.has_value()) {
        return {};
    }

    auto state = session::decode_auth_session_repository_state(*bytes);
    if (!state.has_value()) {
        return {};
    }
    return *state;
}

void AndroidSessionRepository::save_repository_state(const session::AuthSessionRepositoryState& state) {
    if (state.sessions.empty()) {
        bridge_->clear_bytes();
        return;
    }

    auto bytes = session::encode_auth_session_repository_state(state);
    if (bytes.empty()) {
        throw std::runtime_error("failed to encode auth session repository");
    }

    bridge_->save_bytes(std::move(bytes));
}

} // namespace cauth::core::runtime
