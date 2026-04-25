#include "steam/auth/steam_network_authenticator.hpp"

#include <iostream>

namespace {

class FakeTransport final : public cauth::steam::auth::SteamAuthenticationTransport {
  public:
    cauth::steam::auth::SteamTransportResponse<cauth::steam::auth::SteamRsaPublicKey>
    get_password_rsa_public_key(const std::string&) override {
        ++key_calls;
        if (cancel_key_request) {
            return {{false, "operation canceled"}, std::nullopt};
        }
        return {
            {true, ""},
            cauth::steam::auth::SteamRsaPublicKey{"modulus", "010001", 1234},
        };
    }

    cauth::steam::auth::SteamTransportResponse<cauth::steam::auth::SteamBeginAuthSessionResponse>
    begin_auth_session_via_credentials(
        const cauth::steam::auth::SteamBeginAuthSessionRequest& request) override {
        ++begin_calls;
        last_begin_request = request;
        if (cancel_begin_request) {
            return {{false, "operation canceled"}, std::nullopt};
        }
        if (guard_required && request.steam_guard_code.empty()) {
            cauth::steam::auth::SteamBeginAuthSessionResponse response;
            response.interval_seconds = begin_interval_seconds;
            response.allowed_confirmations.push_back(
                {guard_type, "guard"});
            if (guard_type ==
                cauth::steam::auth::SteamGuardConfirmationType::DeviceConfirmation) {
                response.client_id = 42;
                response.request_id = {1, 2, 3};
                response.steam_id = 76561198000000000ULL;
            }
            return {{true, ""}, response};
        }

        cauth::steam::auth::SteamBeginAuthSessionResponse response;
        response.client_id = 42;
        response.request_id = {1, 2, 3};
        response.steam_id = 76561198000000000ULL;
        response.interval_seconds = begin_interval_seconds;
        return {{true, ""}, response};
    }

    cauth::steam::auth::SteamTransportResponse<cauth::steam::auth::SteamPollAuthSessionStatusResponse>
    poll_auth_session_status(
        const cauth::steam::auth::SteamPollAuthSessionStatusRequest& request) override {
        ++poll_calls;
        last_poll_request = request;
        return {
            {true, ""},
            cauth::steam::auth::SteamPollAuthSessionStatusResponse{
                poll_refresh_token,
                poll_access_token,
                poll_account_name,
                poll_had_remote_interaction,
            },
        };
    }

    cauth::steam::auth::SteamTransportResponse<
        cauth::steam::auth::SteamGenerateAccessTokenForAppResponse>
    generate_access_token_for_app(
        const cauth::steam::auth::SteamGenerateAccessTokenForAppRequest&) override {
        return {
            {true, ""},
            cauth::steam::auth::SteamGenerateAccessTokenForAppResponse{
                "access-token",
                "refresh-token",
            },
        };
    }

    bool guard_required = false;
    bool cancel_key_request = false;
    bool cancel_begin_request = false;
    cauth::steam::auth::SteamGuardConfirmationType guard_type =
        cauth::steam::auth::SteamGuardConfirmationType::EmailCode;
    int key_calls = 0;
    int begin_calls = 0;
    int poll_calls = 0;
    double begin_interval_seconds = 0.0;
    std::string poll_refresh_token = "refresh-token";
    std::string poll_access_token = "access-token";
    std::string poll_account_name = "test_account";
    bool poll_had_remote_interaction = false;
    cauth::steam::auth::SteamBeginAuthSessionRequest last_begin_request;
    cauth::steam::auth::SteamPollAuthSessionStatusRequest last_poll_request;
};

class FakeEncryptor final : public cauth::steam::auth::SteamPasswordEncryptor {
  public:
    std::optional<cauth::steam::auth::SteamEncryptedPassword> encrypt_password(
        const std::string& password,
        const cauth::steam::auth::SteamRsaPublicKey& key) override {
        last_password = password;
        last_key = key;
        return cauth::steam::auth::SteamEncryptedPassword{"encrypted-password", key.timestamp};
    }

    std::string last_password;
    cauth::steam::auth::SteamRsaPublicKey last_key;
};

cauth::steam::auth::SteamLoginRequest make_request() {
    return cauth::steam::auth::SteamLoginRequest{
        "test_account",
        "plain-password",
        "",
        "TestCaller",
        true,
        cauth::steam::auth::SteamLoginPlatformType::SteamClient,
    };
}

} // namespace

int main() {
    FakeTransport transport;
    FakeEncryptor encryptor;
    cauth::steam::auth::SteamNetworkAuthenticator authenticator{transport, encryptor};

    const auto result = authenticator.login(make_request());
    if (result.status != cauth::steam::auth::SteamLoginStatus::Succeeded ||
        !result.session.has_value() || result.session->refresh_token != "refresh-token" ||
        result.session->session_type != "steam-client") {
        std::cerr << "network authenticator should complete a token-bearing login\n";
        return 1;
    }

    if (transport.key_calls != 1 || transport.begin_calls != 1 || transport.poll_calls != 1) {
        std::cerr << "network authenticator should call key, begin, and poll exactly once\n";
        return 1;
    }

    if (transport.last_begin_request.encrypted_password != "encrypted-password" ||
        transport.last_begin_request.encryption_timestamp != 1234 ||
        transport.last_begin_request.platform_type !=
            cauth::steam::auth::SteamLoginPlatformType::SteamClient ||
        transport.last_begin_request.device_friendly_name != "TestCaller") {
        std::cerr << "network authenticator should pass encrypted password fields\n";
        return 1;
    }

    transport.guard_required = true;
    transport.guard_type = cauth::steam::auth::SteamGuardConfirmationType::EmailCode;
    const auto guard_result = authenticator.login(make_request());
    if (guard_result.status != cauth::steam::auth::SteamLoginStatus::SteamGuardRequired) {
        std::cerr << "guard challenge should surface as SteamGuardRequired\n";
        return 1;
    }

    auto guarded_request = make_request();
    guarded_request.steam_guard_code = "12345";
    const auto guarded_result = authenticator.login(guarded_request);
    if (guarded_result.status != cauth::steam::auth::SteamLoginStatus::Succeeded ||
        transport.last_begin_request.steam_guard_code != "12345") {
        std::cerr << "guard code should be sent with the begin request\n";
        return 1;
    }

    transport.guard_type = cauth::steam::auth::SteamGuardConfirmationType::DeviceConfirmation;
    const auto mobile_confirmation_result = authenticator.login(make_request());
    if (mobile_confirmation_result.status != cauth::steam::auth::SteamLoginStatus::Succeeded) {
        std::cerr << "mobile confirmation should continue to poll without a typed code\n";
        return 1;
    }

    transport.poll_refresh_token.clear();
    transport.poll_access_token.clear();
    transport.begin_interval_seconds = 0.01;
    bool cancel_requested = false;
    int waiting_calls = 0;
    cauth::steam::auth::SteamNetworkAuthenticator cancelable_authenticator{
        transport,
        encryptor,
        cauth::steam::auth::SteamNetworkAuthenticatorOptions{
            4,
            [&](int, int, double) {
                ++waiting_calls;
                cancel_requested = true;
            },
            [&]() { return cancel_requested; },
        },
    };
    const auto canceled_result = cancelable_authenticator.login(make_request());
    if (canceled_result.status != cauth::steam::auth::SteamLoginStatus::Canceled ||
        waiting_calls != 1) {
        std::cerr << "polling cancel should surface as canceled after the first wait\n";
        return 1;
    }

    transport.poll_refresh_token = "refresh-token";
    transport.cancel_key_request = true;
    const auto canceled_key_result = authenticator.login(make_request());
    if (canceled_key_result.status != cauth::steam::auth::SteamLoginStatus::Canceled) {
        std::cerr << "key request cancellation should surface as canceled\n";
        return 1;
    }

    transport.cancel_key_request = false;
    transport.cancel_begin_request = true;
    const auto canceled_begin_result = authenticator.login(make_request());
    if (canceled_begin_result.status != cauth::steam::auth::SteamLoginStatus::Canceled) {
        std::cerr << "begin request cancellation should surface as canceled\n";
        return 1;
    }

#ifdef _WIN32
    cauth::steam::auth::PlatformSteamPasswordEncryptor platform_encryptor;
    const auto encrypted = platform_encryptor.encrypt_password(
        "plain-password",
        cauth::steam::auth::SteamRsaPublicKey{
            "d1d8fe310beed284cd05a90adf0f0f5dcd37b612311812762cbf2abe870ffe4eff343e1a4ba8e797"
            "439189cafd2d091af9678edf63f1b0bef36af97ae7612d549f4a4a856ba0939a63a1572b2ab7645"
            "a2f2f6e36c72765806f65ffcf0ca070185a7784dce76148fb7124e56bd99ee304b19bc6e70298c"
            "53ab70a4263e95839d5",
            "010001",
            1234,
        });

    if (!encrypted.has_value() || encrypted->bytes.size() != 172 || encrypted->timestamp != 1234 ||
        encrypted->bytes == "plain-password" || encrypted->bytes.find('=') == std::string::npos) {
        std::cerr << "platform encryptor should produce base64 1024-bit RSA ciphertext\n";
        return 1;
    }
#endif

    return 0;
}
