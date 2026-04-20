#include "steam/auth/steam_login_service.hpp"
#include "core/runtime/session/memory_session_repository.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <iostream>

namespace {

class FakeSteamAuthenticator final : public cauth::steam::auth::SteamAuthenticator {
  public:
    cauth::steam::auth::SteamLoginResult login(const cauth::steam::auth::SteamLoginRequest& request) override {
        last_request = request;
        if (needs_guard && request.steam_guard_code.empty()) {
            return cauth::steam::auth::SteamLoginResult{
                cauth::steam::auth::SteamLoginStatus::SteamGuardRequired,
                std::nullopt,
                "Steam Guard code required",
            };
        }

        return cauth::steam::auth::SteamLoginResult{
            cauth::steam::auth::SteamLoginStatus::Succeeded,
            cauth::core::session::AuthSession{
                std::string{cauth::steam::auth::kSteamAuthProvider},
                "76561198000000000",
                request.account_name,
                "refresh-token-for-test",
                "access-token-for-test",
                "steam-client",
            },
            "ok",
        };
    }

    bool needs_guard = false;
    cauth::steam::auth::SteamLoginRequest last_request;
};

cauth::steam::auth::SteamLoginRequest make_request() {
    return cauth::steam::auth::SteamLoginRequest{
        "test_account",
        "password-for-test",
        "",
        "CAuth Test",
        true,
        cauth::steam::auth::SteamLoginPlatformType::SteamClient,
    };
}

} // namespace

int main() {
    FakeSteamAuthenticator authenticator;
    cauth::core::runtime::MemorySessionRepository store;
    cauth::steam::auth::SteamLoginService service{authenticator, store};

    auto invalid = make_request();
    invalid.password.clear();
    const auto invalid_result = service.login(invalid);
    if (invalid_result.status != cauth::steam::auth::SteamLoginStatus::Failed ||
        invalid_result.message != "password is required") {
        std::cerr << "missing password should fail before authenticator call\n";
        return 1;
    }

    auto caller_named_request = make_request();
    caller_named_request.device_name = "ExampleLauncher";
    const auto caller_named_result = service.login(caller_named_request);
    if (caller_named_result.status != cauth::steam::auth::SteamLoginStatus::Succeeded ||
        authenticator.last_request.device_name != "ExampleLauncher") {
        std::cerr << "custom device name should be passed through unchanged\n";
        return 1;
    }

    store.clear_all_auth_sessions();
    auto default_device_name_request = make_request();
    default_device_name_request.device_name.clear();
    const auto default_device_name_result = service.login(default_device_name_request);
    if (default_device_name_result.status != cauth::steam::auth::SteamLoginStatus::Succeeded ||
        authenticator.last_request.device_name != "CAuth") {
        std::cerr << "empty device name should fall back to CAuth\n";
        return 1;
    }

    store.clear_all_auth_sessions();

    authenticator.needs_guard = true;
    const auto guard_result = service.login(make_request());
    if (guard_result.status != cauth::steam::auth::SteamLoginStatus::SteamGuardRequired ||
        !store.list_auth_sessions().empty()) {
        std::cerr << "Steam Guard challenge should not save a session\n";
        return 1;
    }

    auto guarded_request = make_request();
    guarded_request.steam_guard_code = "12345";
    const auto login_result = service.login(guarded_request);
    if (login_result.status != cauth::steam::auth::SteamLoginStatus::Succeeded) {
        std::cerr << "valid login should succeed\n";
        return 1;
    }

    const auto saved =
        store.load_auth_session(cauth::steam::auth::kSteamAuthProvider, "76561198000000000");
    if (!saved.has_value() || saved->account_name != "test_account" ||
        saved->refresh_token != "refresh-token-for-test" ||
        saved->access_token != "access-token-for-test") {
        std::cerr << "successful login should save the auth session\n";
        return 1;
    }

    if (saved->session_type != "steam-client") {
        std::cerr << "successful login should preserve the session type\n";
        return 1;
    }

    store.clear_all_auth_sessions();
    authenticator.needs_guard = false;
    auto transient_request = make_request();
    transient_request.remember_session = false;
    const auto transient_result = service.login(transient_request);
    if (transient_result.status != cauth::steam::auth::SteamLoginStatus::Succeeded ||
        !store.list_auth_sessions().empty()) {
        std::cerr << "transient login should not save the auth session\n";
        return 1;
    }

    return 0;
}
