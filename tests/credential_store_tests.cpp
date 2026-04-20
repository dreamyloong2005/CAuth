#include "core/session/auth_session.hpp"
#include "core/session/auth_session_codec.hpp"
#include "core/runtime/session/android_session_repository.hpp"
#include "core/runtime/session/memory_session_repository.hpp"
#include "core/runtime/windows/windows_session_repository.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

cauth::core::session::AuthSession make_session(std::string subject_id,
                                               std::string account_name,
                                               std::string refresh_token) {
    return cauth::core::session::AuthSession{
        std::string{cauth::steam::auth::kSteamAuthProvider},
        std::move(subject_id),
        std::move(account_name),
        std::move(refresh_token),
        "access-token-for-test",
    };
}

class TestAndroidBridge final : public cauth::core::runtime::AndroidSecureStorageBridge {
  public:
    void save_bytes(std::vector<std::uint8_t> bytes) override {
        bytes_ = std::move(bytes);
    }

    std::optional<std::vector<std::uint8_t>> load_bytes() const override {
        return bytes_;
    }

    void clear_bytes() override {
        bytes_.reset();
    }

  private:
    std::optional<std::vector<std::uint8_t>> bytes_;
};

bool expect_session_round_trip(cauth::core::session::SessionRepository& store) {
    const auto session = make_session();
    store.save_auth_session(session);

    const auto loaded =
        store.load_auth_session(cauth::steam::auth::kSteamAuthProvider, session.subject_id);
    if (!loaded.has_value()) {
        std::cerr << "saved session was not loaded\n";
        return false;
    }

    if (cauth::steam::auth::steam_id(*loaded) != cauth::steam::auth::steam_id(session) ||
        loaded->account_name != session.account_name ||
        loaded->refresh_token != session.refresh_token ||
        loaded->access_token != session.access_token) {
        std::cerr << "loaded session does not match saved session\n";
        return false;
    }

    store.clear_auth_session(cauth::steam::auth::kSteamAuthProvider, session.subject_id);
    if (store.load_auth_session(cauth::steam::auth::kSteamAuthProvider, session.subject_id).has_value()) {
        std::cerr << "cleared store should not contain a session\n";
        return false;
    }

    return true;
}

bool expect_multi_account_repository(cauth::core::session::SessionRepository& store) {
    store.clear_all_auth_sessions();

    const auto first = make_session("76561198000000001", "first_account", "first-refresh");
    const auto second = make_session("76561198000000002", "second_account", "second-refresh");
    auto first_updated = first;
    first_updated.refresh_token = "first-refresh-updated";

    store.save_auth_session(first);
    store.save_auth_session(first_updated);
    store.save_auth_session(second);

    const auto sessions = store.list_auth_sessions();
    if (sessions.size() != 2) {
        std::cerr << "repository should contain two saved accounts\n";
        return false;
    }

    const auto loaded_first =
        store.load_auth_session(cauth::steam::auth::kSteamAuthProvider, first.subject_id);
    if (!loaded_first.has_value() || loaded_first->account_name != first.account_name ||
        loaded_first->refresh_token != first_updated.refresh_token) {
        std::cerr << "repository should load a saved account by key\n";
        return false;
    }

    const auto loaded_second =
        store.load_auth_session(cauth::steam::auth::kSteamAuthProvider, second.subject_id);
    if (!loaded_second.has_value() || loaded_second->account_name != second.account_name ||
        loaded_second->refresh_token != second.refresh_token) {
        std::cerr << "repository should load a second saved account by key\n";
        return false;
    }

    store.clear_auth_session(cauth::steam::auth::kSteamAuthProvider, first.subject_id);
    if (store.load_auth_session(cauth::steam::auth::kSteamAuthProvider, first.subject_id).has_value()) {
        std::cerr << "removing one account should remove that explicit key\n";
        return false;
    }
    if (store.list_auth_sessions().size() != 1) {
        std::cerr << "removing one account should keep the remaining account\n";
        return false;
    }
    if (!store.load_auth_session(cauth::steam::auth::kSteamAuthProvider, second.subject_id).has_value()) {
        std::cerr << "remaining account should still load by explicit key\n";
        return false;
    }

    store.clear_all_auth_sessions();
    if (!store.list_auth_sessions().empty() ||
        store.load_auth_session(cauth::steam::auth::kSteamAuthProvider, second.subject_id).has_value()) {
        std::cerr << "clear_all_auth_sessions should empty the repository\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    cauth::core::runtime::MemorySessionRepository store;

    if (store.load_auth_session(cauth::steam::auth::kSteamAuthProvider, "76561198000000000")
            .has_value()) {
        std::cerr << "new store should not contain a session\n";
        return 1;
    }

    const auto session = make_session();
    if (!cauth::core::session::is_valid(session)) {
        std::cerr << "test session should be valid\n";
        return 1;
    }

    const auto encoded = cauth::core::session::encode_auth_session(session);
    const auto decoded = cauth::core::session::decode_auth_session(encoded);
    if (!decoded.has_value() ||
        cauth::steam::auth::steam_id(*decoded) != cauth::steam::auth::steam_id(session) ||
        decoded->account_name != session.account_name ||
        decoded->refresh_token != session.refresh_token) {
        std::cerr << "session codec should round-trip valid sessions\n";
        return 1;
    }

    cauth::core::session::AuthSessionRepositoryState state;
    const auto second_session =
        make_session("76561198000000003", "second_account", "second-refresh");
    cauth::core::session::upsert_auth_session(state, session);
    cauth::core::session::upsert_auth_session(state, second_session);
    const auto encoded_state =
        cauth::core::session::encode_auth_session_repository_state(state);
    const auto decoded_state =
        cauth::core::session::decode_auth_session_repository_state(encoded_state);
    if (!decoded_state.has_value() || decoded_state->sessions.size() != 2) {
        std::cerr << "repository state codec should round-trip multiple accounts\n";
        return 1;
    }

    const auto legacy_state =
        cauth::core::session::decode_auth_session_repository_state(encoded);
    if (!legacy_state.has_value() || legacy_state->sessions.size() != 1) {
        std::cerr << "repository state codec should migrate legacy single-session data\n";
        return 1;
    }

    if (!expect_session_round_trip(store)) {
        return 1;
    }
    if (!expect_multi_account_repository(store)) {
        return 1;
    }

    if (cauth::core::session::redacted_account_label(session) != "t***t") {
        std::cerr << "account label should be redacted\n";
        return 1;
    }

    TestAndroidBridge bridge;
    cauth::core::runtime::AndroidSessionRepository android_store{bridge};
    if (!expect_session_round_trip(android_store)) {
        return 1;
    }
    if (!expect_multi_account_repository(android_store)) {
        return 1;
    }

#ifdef _WIN32
    const auto temp_path = std::filesystem::temp_directory_path() / "cauth_credential_store_test.dpapi";
    cauth::core::runtime::WindowsSessionRepository windows_store{temp_path};
    windows_store.clear_all_auth_sessions();
    if (!expect_session_round_trip(windows_store)) {
        return 1;
    }
    if (!expect_multi_account_repository(windows_store)) {
        return 1;
    }
#endif

    return 0;
}
