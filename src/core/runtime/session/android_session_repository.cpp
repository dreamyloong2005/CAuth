#include "core/runtime/session/android_session_repository.hpp"

#include "core/session/auth_session_codec.hpp"

#include <stdexcept>

namespace cauth::core::runtime {

AndroidSessionRepository::AndroidSessionRepository(AndroidSecureStorageBridge& bridge)
    : bridge_(&bridge) {}

void AndroidSessionRepository::save_auth_session(const session::AuthSession& session) {
    auto bytes = session::encode_auth_session(session);
    if (bytes.empty()) {
        throw std::runtime_error("failed to encode auth session");
    }

    bridge_->save_bytes(std::move(bytes));
}

std::optional<session::AuthSession> AndroidSessionRepository::load_auth_session() const {
    const auto bytes = bridge_->load_bytes();
    if (!bytes.has_value()) {
        return std::nullopt;
    }

    return session::decode_auth_session(*bytes);
}

void AndroidSessionRepository::clear_auth_session() {
    bridge_->clear_bytes();
}

} // namespace cauth::core::runtime
