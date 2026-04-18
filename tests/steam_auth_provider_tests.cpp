#include "core/runtime/session/memory_session_repository.hpp"
#include "steam/auth/steam_auth_provider.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <iostream>

namespace {

cauth::core::session::AuthSession make_session() {
    return cauth::core::session::AuthSession{
        std::string{cauth::steam::auth::kSteamAuthProvider},
        "76561198000000000",
        "test_account",
        "refresh-token-for-test",
        "access-token-for-test",
    };
}

} // namespace

int main() {
    cauth::core::runtime::MemorySessionRepository store;
    cauth::steam::auth::StoredSteamAuthProvider provider{store};

    const auto empty = provider.load_auth_session();
    if (empty.ok || empty.session.has_value()) {
        std::cerr << "missing session should not load successfully\n";
        return 1;
    }

    auto session = make_session();
    store.save_auth_session(session);
    const auto loaded = provider.load_auth_session();
    if (!loaded.ok || !loaded.session.has_value()) {
        std::cerr << "valid saved session should load successfully\n";
        return 1;
    }

    if (cauth::steam::auth::steam_id(*loaded.session) !=
            cauth::steam::auth::steam_id(session) ||
        loaded.session->account_name != session.account_name) {
        std::cerr << "loaded auth session should match saved session\n";
        return 1;
    }

    session.subject_id = "not-a-steam-id";
    store.save_auth_session(session);
    const auto bad_subject = provider.load_auth_session();
    if (bad_subject.ok || bad_subject.session.has_value()) {
        std::cerr << "session without a valid Steam subject should be rejected\n";
        return 1;
    }

    return 0;
}
