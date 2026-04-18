#include "core/session/auth_session.hpp"
#include "core/session/auth_session_codec.hpp"
#include "core/runtime/session/android_session_repository.hpp"
#include "core/runtime/session/memory_session_repository.hpp"
#include "core/runtime/windows/windows_session_repository.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
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

    const auto loaded = store.load_auth_session();
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

    store.clear_auth_session();
    if (store.load_auth_session().has_value()) {
        std::cerr << "cleared store should not contain a session\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    cauth::core::runtime::MemorySessionRepository store;

    if (store.load_auth_session().has_value()) {
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

    if (!expect_session_round_trip(store)) {
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

#ifdef _WIN32
    const auto temp_path = std::filesystem::temp_directory_path() / "cauth_credential_store_test.dpapi";
    cauth::core::runtime::WindowsSessionRepository windows_store{temp_path};
    windows_store.clear_auth_session();
    if (!expect_session_round_trip(windows_store)) {
        return 1;
    }
#endif

    return 0;
}
