#include "steam/auth/steam_mobile_app_login.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <iostream>

namespace {

class FakeAuthenticator final : public cauth::steam::auth::SteamAuthenticator {
  public:
    cauth::steam::auth::SteamLoginResult login(
        const cauth::steam::auth::SteamLoginRequest& request) override {
        last_request = request;
        if (status != cauth::steam::auth::SteamLoginStatus::Succeeded) {
            return {status, std::nullopt, message};
        }

        cauth::core::session::AuthSession session;
        session.provider = std::string{cauth::steam::auth::kSteamAuthProvider};
        session.subject_id = "76561198000000000";
        session.account_name = request.account_name;
        session.refresh_token = "refresh-token";
        session.access_token = access_token;
        return {status, session, message};
    }

    cauth::steam::auth::SteamLoginRequest last_request;
    cauth::steam::auth::SteamLoginStatus status = cauth::steam::auth::SteamLoginStatus::Succeeded;
    std::string message = "ok";
    std::string access_token;
};

class FakeTransport final : public cauth::steam::auth::SteamAuthenticationTransport {
  public:
    cauth::steam::auth::SteamTransportResponse<cauth::steam::auth::SteamRsaPublicKey>
    get_password_rsa_public_key(const std::string&) override {
        return {{false, "unexpected"}, std::nullopt};
    }

    cauth::steam::auth::SteamTransportResponse<cauth::steam::auth::SteamBeginAuthSessionResponse>
    begin_auth_session_via_credentials(
        const cauth::steam::auth::SteamBeginAuthSessionRequest&) override {
        return {{false, "unexpected"}, std::nullopt};
    }

    cauth::steam::auth::SteamTransportResponse<cauth::steam::auth::SteamPollAuthSessionStatusResponse>
    poll_auth_session_status(
        const cauth::steam::auth::SteamPollAuthSessionStatusRequest&) override {
        return {{false, "unexpected"}, std::nullopt};
    }

    cauth::steam::auth::SteamTransportResponse<
        cauth::steam::auth::SteamGenerateAccessTokenForAppResponse>
    generate_access_token_for_app(
        const cauth::steam::auth::SteamGenerateAccessTokenForAppRequest& request) override {
        ++generate_calls;
        last_request = request;
        if (!generate_ok) {
            return {{false, generate_error}, std::nullopt};
        }
        return {{true, ""},
                cauth::steam::auth::SteamGenerateAccessTokenForAppResponse{
                    generated_access_token,
                    generated_refresh_token,
                }};
    }

    bool generate_ok = true;
    std::string generate_error;
    std::string generated_access_token = "mobile-access-token";
    std::string generated_refresh_token = "mobile-refresh-token";
    int generate_calls = 0;
    cauth::steam::auth::SteamGenerateAccessTokenForAppRequest last_request;
};

cauth::steam::auth::SteamLoginRequest make_request(cauth::steam::auth::SteamLoginPlatformType type) {
    cauth::steam::auth::SteamLoginRequest request;
    request.account_name = "test_account";
    request.password = "password";
    request.device_name = "TestDevice_CAuth";
    request.platform_type = type;
    return request;
}

} // namespace

int main() {
    FakeAuthenticator inner;
    FakeTransport transport;
    cauth::steam::auth::SteamMobileAppLogin login{inner, transport};

    const auto mobile_result = login.login(
        make_request(cauth::steam::auth::SteamLoginPlatformType::MobileApp));
    if (mobile_result.status != cauth::steam::auth::SteamLoginStatus::Succeeded ||
        !mobile_result.session.has_value() ||
        mobile_result.session->access_token != "mobile-access-token" ||
        mobile_result.session->refresh_token != "mobile-refresh-token" ||
        transport.generate_calls != 1 ||
        transport.last_request.platform_type !=
            cauth::steam::auth::SteamLoginPlatformType::MobileApp) {
        std::cerr << "mobile app login should upgrade refresh token into mobile access token\n";
        return 1;
    }

    inner.access_token = "already-present";
    transport.generate_calls = 0;
    const auto no_refresh_needed = login.login(
        make_request(cauth::steam::auth::SteamLoginPlatformType::MobileApp));
    if (!no_refresh_needed.session.has_value() ||
        no_refresh_needed.session->access_token != "already-present" ||
        transport.generate_calls != 0) {
        std::cerr << "mobile app login should reuse access token when already present\n";
        return 1;
    }

    inner.access_token.clear();
    const auto web_result = login.login(
        make_request(cauth::steam::auth::SteamLoginPlatformType::WebBrowser));
    if (web_result.status != cauth::steam::auth::SteamLoginStatus::Succeeded ||
        !web_result.session.has_value() ||
        !web_result.session->access_token.empty()) {
        std::cerr << "non-mobile login should bypass mobile access token generation\n";
        return 1;
    }

    transport.generate_ok = false;
    transport.generate_error = "AccessDenied";
    const auto failed = login.login(
        make_request(cauth::steam::auth::SteamLoginPlatformType::MobileApp));
    if (failed.status != cauth::steam::auth::SteamLoginStatus::Failed ||
        failed.message.find("GenerateAccessTokenForApp failed") == std::string::npos) {
        std::cerr << "mobile app token generation failures should surface clearly\n";
        return 1;
    }

    return 0;
}
