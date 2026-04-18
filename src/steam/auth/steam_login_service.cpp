#include "steam/auth/steam_login_service.hpp"

#include "steam/auth/device_name.hpp"

namespace cauth::steam::auth {

SteamLoginService::SteamLoginService(SteamAuthenticator& authenticator,
                                     cauth::core::session::AuthSessionWriter& session_writer)
    : authenticator_(&authenticator), session_writer_(&session_writer) {}

SteamLoginResult SteamLoginService::login(const SteamLoginRequest& request) {
    if (request.account_name.empty()) {
        return SteamLoginResult{SteamLoginStatus::Failed, std::nullopt, "account name is required"};
    }

    if (request.password.empty()) {
        return SteamLoginResult{SteamLoginStatus::Failed, std::nullopt, "password is required"};
    }

    auto normalized_request = request;
    normalized_request.device_name = normalize_device_name(request.device_name);

    auto result = authenticator_->login(normalized_request);
    if (result.status == SteamLoginStatus::Succeeded && result.session.has_value()) {
        if (!cauth::core::session::is_valid(*result.session)) {
            return SteamLoginResult{SteamLoginStatus::Failed, std::nullopt,
                               "authenticator returned an invalid session"};
        }

        if (request.remember_session) {
            session_writer_->save_auth_session(*result.session);
        }
    }

    return result;
}

} // namespace cauth::steam::auth
