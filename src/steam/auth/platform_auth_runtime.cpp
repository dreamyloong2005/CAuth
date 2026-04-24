#include "steam/auth/platform_auth_runtime.hpp"

#include "steam/auth/cm_authentication_transport.hpp"
#include "steam/auth/steam_login_service.hpp"
#include "steam/auth/steam_mobile_app_login.hpp"
#include "steam/auth/steam_network_authenticator.hpp"
#include "steam/auth/steam_web_api_auth_transport.hpp"
#include "steam/auth/unimplemented_steam_auth_transport.hpp"
#include "steam/cm/cm_service_method.hpp"
#include "steam/cm/steam_cm_connector.hpp"
#include "core/platform/session_repository_factory.hpp"
#include "core/platform/http_client.hpp"
#include "core/platform/password_crypto.hpp"

namespace cauth::steam::auth {
namespace {

SteamLoginResult login_with_web_api_auth(cauth::core::session::AuthSessionWriter& session_writer,
                                         const SteamLoginRequest& request,
                                         const SteamPlatformLoginOptions& options) {
    SteamWebApiAuthenticationTransport transport;
    PlatformSteamPasswordEncryptor password_encryptor;
    SteamNetworkAuthenticator network_authenticator{transport, password_encryptor,
                                                    options.authenticator_options};
    std::unique_ptr<SteamAuthenticator> authenticator;
    if (request.platform_type == SteamLoginPlatformType::MobileApp) {
        authenticator = std::make_unique<SteamMobileAppLogin>(network_authenticator, transport);
    } else {
        authenticator = std::make_unique<SteamNetworkAuthenticator>(
            transport, password_encryptor, options.authenticator_options);
    }
    SteamLoginService service{*authenticator, session_writer};
    return service.login(request);
}

SteamLoginResult login_with_cm_auth(cauth::core::session::AuthSessionWriter& session_writer,
                                    const SteamLoginRequest& request,
                                    const SteamPlatformLoginOptions& options) {
    std::optional<SteamLoginResult> last_failure;
    std::optional<SteamLoginResult> terminal_result;
    cauth::core::cm::SteamCmConnector connector;
    const auto result = connector.with_service_client(
        options.cm_max_count,
        request.route_selection.empty() ? nullptr : &request.route_selection,
        [&](const cauth::core::cm::CmServerEndpoint&, cauth::core::cm::CmServiceMethodClient& service_client) {
        CmAuthenticationTransport auth_transport{service_client};
        PlatformSteamPasswordEncryptor password_encryptor;
        SteamNetworkAuthenticator authenticator{auth_transport, password_encryptor,
                                                options.authenticator_options};
        SteamLoginService service{authenticator, session_writer};
        auto login_result = service.login(request);

        if (login_result.status == SteamLoginStatus::Succeeded ||
            login_result.status == SteamLoginStatus::SteamGuardRequired) {
            terminal_result = login_result;
            return cauth::core::cm::SteamCmAttemptResult{
                cauth::core::cm::SteamCmContinuation::Stop, true, login_result.message};
        }

        if (login_result.message.find("RateLimitExceeded") != std::string::npos ||
            login_result.message.find("AccountLoginDeniedThrottle") != std::string::npos) {
            terminal_result = login_result;
            return cauth::core::cm::SteamCmAttemptResult{
                cauth::core::cm::SteamCmContinuation::Stop, false, login_result.message};
        }

        last_failure = std::move(login_result);
        return cauth::core::cm::SteamCmAttemptResult{
            cauth::core::cm::SteamCmContinuation::Continue,
            false,
            last_failure->message,
        };
    });

    if (terminal_result.has_value()) {
        return *terminal_result;
    }

    if (!result.ok && !result.error_message.empty() && !last_failure.has_value()) {
        return {SteamLoginStatus::Failed, std::nullopt, result.error_message};
    }

    return last_failure.value_or(SteamLoginResult{
        SteamLoginStatus::Failed,
        std::nullopt,
        result.error_message.empty() ? "CM auth failed for all endpoints" : result.error_message,
    });
}

} // namespace

PlatformAuthRuntime make_steam_platform_auth_runtime() {
    PlatformAuthRuntime runtime;
    if (cauth::core::platform::is_platform_http_client_available()) {
        runtime.transport = std::make_unique<SteamWebApiAuthenticationTransport>();
    } else {
        runtime.transport = std::make_unique<UnimplementedSteamAuthenticationTransport>();
    }

    if (cauth::core::platform::is_platform_password_crypto_available()) {
        runtime.password_encryptor = std::make_unique<PlatformSteamPasswordEncryptor>();
    } else {
        runtime.password_encryptor = std::make_unique<UnsupportedSteamPasswordEncryptor>();
    }

    runtime.authenticator = std::make_unique<SteamNetworkAuthenticator>(*runtime.transport,
                                                                        *runtime.password_encryptor);
    return runtime;
}

SteamLoginResult login_with_steam_platform_auth(
    cauth::core::session::AuthSessionWriter& session_writer,
    const SteamLoginRequest& request,
    const SteamPlatformLoginOptions& options) {
    if (request.platform_type == SteamLoginPlatformType::WebBrowser) {
        return login_with_web_api_auth(session_writer, request, options);
    }
    if (request.platform_type == SteamLoginPlatformType::MobileApp) {
        if (!cauth::core::platform::is_platform_http_client_available()) {
            return {SteamLoginStatus::Unsupported,
                    std::nullopt,
                    "Mobile app login requires the platform HTTP client"};
        }
        return login_with_web_api_auth(session_writer, request, options);
    }

#ifdef __ANDROID__
    return login_with_cm_auth(session_writer, request, options);
#else
    if (cauth::core::platform::is_platform_http_client_available() &&
        request.platform_type != SteamLoginPlatformType::SteamClient) {
        return login_with_web_api_auth(session_writer, request, options);
    }
    return login_with_cm_auth(session_writer, request, options);
#endif
}

SteamLoginResult login_with_steam_platform_auth(
    cauth::core::session::AuthSessionWriter& session_writer,
    const SteamLoginRequest& request) {
    return login_with_steam_platform_auth(session_writer, request, SteamPlatformLoginOptions{});
}

} // namespace cauth::steam::auth

