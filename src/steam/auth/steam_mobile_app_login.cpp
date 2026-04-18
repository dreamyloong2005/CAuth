#include "steam/auth/steam_mobile_app_login.hpp"

#include "steam/auth/steam_session_identity.hpp"

namespace cauth::steam::auth {

SteamMobileAppLogin::SteamMobileAppLogin(SteamAuthenticator& inner,
                                         SteamAuthenticationTransport& transport)
    : inner_(&inner), transport_(&transport) {}

SteamLoginResult SteamMobileAppLogin::login(const SteamLoginRequest& request) {
    auto result = inner_->login(request);
    if (request.platform_type != SteamLoginPlatformType::MobileApp ||
        result.status != SteamLoginStatus::Succeeded || !result.session.has_value()) {
        return result;
    }

    if (!result.session->access_token.empty()) {
        return result;
    }

    const auto token = transport_->generate_access_token_for_app(
        SteamGenerateAccessTokenForAppRequest{
            steam_id(*result.session),
            result.session->refresh_token,
            SteamLoginPlatformType::MobileApp,
        });
    if (!token.result.ok || !token.value.has_value()) {
        auto message = std::string{"GenerateAccessTokenForApp failed"};
        if (!token.result.error_message.empty()) {
            message += ": " + token.result.error_message;
        }
        return {SteamLoginStatus::Failed, std::nullopt, message};
    }

    if (token.value->access_token.empty()) {
        return {SteamLoginStatus::Failed,
                std::nullopt,
                "GenerateAccessTokenForApp did not return an access token"};
    }

    result.session->access_token = token.value->access_token;
    if (!token.value->refresh_token.empty()) {
        result.session->refresh_token = token.value->refresh_token;
    }
    return result;
}

} // namespace cauth::steam::auth
