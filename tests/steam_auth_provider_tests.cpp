#include "core/runtime/session/memory_session_repository.hpp"
#include "steam/auth/steam_auth_provider.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <utility>

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

cauth::core::session::AuthSession make_session(std::string session_type,
                                               std::string refresh_token,
                                               std::chrono::system_clock::time_point created_at) {
    auto session = make_session();
    session.session_type = std::move(session_type);
    session.refresh_token = std::move(refresh_token);
    session.created_at = created_at;
    return session;
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

    store.clear_all_auth_sessions();
    const auto client_session = make_session(
        std::string{cauth::steam::auth::kSteamSessionTypeSteamClient},
        "client-refresh",
        std::chrono::system_clock::time_point{std::chrono::seconds{100}});
    const auto web_session = make_session(
        std::string{cauth::steam::auth::kSteamSessionTypeWebBrowser},
        "web-refresh",
        std::chrono::system_clock::time_point{std::chrono::seconds{200}});
    store.save_auth_session(client_session);
    store.save_auth_session(web_session);
    const auto selected_client = provider.load_auth_session();
    if (!selected_client.ok || !selected_client.session.has_value() ||
        selected_client.session->refresh_token != "client-refresh") {
        std::cerr << "Steam auth provider should prefer the steam-client session slot\n";
        return 1;
    }

    store.clear_all_auth_sessions();
    store.save_auth_session(web_session);
    const auto web_only = provider.load_auth_session();
    if (web_only.ok || web_only.session.has_value()) {
        std::cerr << "Steam auth provider should reject web-only sessions for CM/depot auth\n";
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
