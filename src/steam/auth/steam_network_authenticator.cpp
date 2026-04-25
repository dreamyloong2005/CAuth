#include "steam/auth/steam_network_authenticator.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace cauth::steam::auth {
namespace {

std::string steam_session_type_name(SteamLoginPlatformType type) {
    switch (type) {
    case SteamLoginPlatformType::MobileApp:
        return std::string{kSteamSessionTypeMobileApp};
    case SteamLoginPlatformType::WebBrowser:
        return std::string{kSteamSessionTypeWebBrowser};
    case SteamLoginPlatformType::SteamClient:
    default:
        return std::string{kSteamSessionTypeSteamClient};
    }
}

bool needs_typed_guard_code(const SteamBeginAuthSessionResponse& response) {
    return std::any_of(response.allowed_confirmations.begin(), response.allowed_confirmations.end(),
                       [](const SteamAllowedConfirmation& confirmation) {
                           return confirmation.type == SteamGuardConfirmationType::EmailCode ||
                                  confirmation.type == SteamGuardConfirmationType::DeviceCode;
                       });
}

bool has_remote_confirmation(const SteamBeginAuthSessionResponse& response) {
    return std::any_of(response.allowed_confirmations.begin(), response.allowed_confirmations.end(),
                       [](const SteamAllowedConfirmation& confirmation) {
                           return confirmation.type ==
                                      SteamGuardConfirmationType::DeviceConfirmation ||
                                  confirmation.type ==
                                      SteamGuardConfirmationType::EmailConfirmation;
                       });
}

bool steam_auth_debug_enabled() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t value_length = 0;
    if (_dupenv_s(&value, &value_length, "CAUTH_DEBUG_STEAM_AUTH") != 0 || value == nullptr) {
        return false;
    }

    const std::string debug_value{value};
    std::free(value);
    return value_length > 0 && debug_value == "1";
#else
    const auto* value = std::getenv("CAUTH_DEBUG_STEAM_AUTH");
    return value != nullptr && std::string_view{value} == "1";
#endif
}

SteamLoginResult transport_failure(const std::string& operation, const SteamTransportResult& result) {
    if (result.error_message.find("operation canceled") != std::string::npos) {
        return SteamLoginResult{
            SteamLoginStatus::Canceled,
            std::nullopt,
            operation + " canceled",
        };
    }

    auto message = operation + " failed";
    if (!result.error_message.empty()) {
        message += ": " + result.error_message;
    }

    return SteamLoginResult{SteamLoginStatus::Failed, std::nullopt, message};
}

bool cancel_requested(const SteamNetworkAuthenticatorOptions& options) {
    return options.cancel_requested && options.cancel_requested();
}

SteamLoginResult login_canceled() {
    return SteamLoginResult{
        SteamLoginStatus::Canceled,
        std::nullopt,
        "Steam authentication canceled",
    };
}

bool wait_for_poll_interval(double interval_seconds, const SteamNetworkAuthenticatorOptions& options) {
    using namespace std::chrono;

    auto remaining = duration_cast<milliseconds>(duration<double>(interval_seconds));
    constexpr auto kSlice = milliseconds{100};
    while (remaining.count() > 0) {
        if (cancel_requested(options)) {
            return false;
        }
        const auto next = std::min(remaining, kSlice);
        std::this_thread::sleep_for(next);
        remaining -= next;
    }
    return !cancel_requested(options);
}

} // namespace

SteamNetworkAuthenticator::SteamNetworkAuthenticator(SteamAuthenticationTransport& transport,
                                                     SteamPasswordEncryptor& password_encryptor,
                                                     SteamNetworkAuthenticatorOptions options)
    : transport_(&transport), password_encryptor_(&password_encryptor), options_(options) {}

SteamLoginResult SteamNetworkAuthenticator::login(const SteamLoginRequest& request) {
    const auto key_response = transport_->get_password_rsa_public_key(request.account_name);
    if (!key_response.result.ok || !key_response.value.has_value()) {
        return transport_failure("GetPasswordRSAPublicKey", key_response.result);
    }

    const auto encrypted_password =
        password_encryptor_->encrypt_password(request.password, *key_response.value);
    if (!encrypted_password.has_value()) {
        return SteamLoginResult{SteamLoginStatus::Unsupported, std::nullopt,
                           "Steam password RSA encryption is not implemented yet"};
    }

    SteamBeginAuthSessionRequest begin_request;
    begin_request.account_name = request.account_name;
    begin_request.encrypted_password = encrypted_password->bytes;
    begin_request.encryption_timestamp = encrypted_password->timestamp;
    begin_request.steam_guard_code = request.steam_guard_code;
    begin_request.remember_login = request.remember_session;
    begin_request.platform_type = request.platform_type;
    begin_request.device_friendly_name = request.device_name.empty() ? "CAuth" : request.device_name;

    const auto begin_response = transport_->begin_auth_session_via_credentials(begin_request);
    if (!begin_response.result.ok || !begin_response.value.has_value()) {
        return transport_failure("BeginAuthSessionViaCredentials", begin_response.result);
    }

    const auto& begin = *begin_response.value;
    if (needs_typed_guard_code(begin) && !has_remote_confirmation(begin) &&
        request.steam_guard_code.empty()) {
        return SteamLoginResult{SteamLoginStatus::SteamGuardRequired, std::nullopt,
                           "Steam Guard confirmation is required"};
    }

    if (begin.client_id == 0 || begin.request_id.empty()) {
        auto message = begin.extended_error_message.empty()
                           ? "Steam did not return a pollable auth session"
                           : begin.extended_error_message;
        return SteamLoginResult{SteamLoginStatus::Failed, std::nullopt, message};
    }

    SteamPollAuthSessionStatusRequest poll_request;
    poll_request.client_id = begin.client_id;
    poll_request.request_id = begin.request_id;
    poll_request.platform_type = request.platform_type;

    const auto poll_attempts = std::max(options_.max_poll_attempts, 1);
    for (int attempt = 0; attempt < poll_attempts; ++attempt) {
        if (cancel_requested(options_)) {
            return login_canceled();
        }

        const auto poll_response = transport_->poll_auth_session_status(poll_request);
        if (!poll_response.result.ok || !poll_response.value.has_value()) {
            return transport_failure("PollAuthSessionStatus", poll_response.result);
        }

        const auto& poll = *poll_response.value;
        if (steam_auth_debug_enabled()) {
            std::cerr << "Steam auth poll " << (attempt + 1) << '/' << poll_attempts
                      << ": refresh_token_len=" << poll.refresh_token.size()
                      << " access_token_len=" << poll.access_token.size()
                      << " account_name=" << poll.account_name
                      << " had_remote_interaction="
                      << (poll.had_remote_interaction ? "true" : "false") << '\n';
        }
        if (!poll.refresh_token.empty()) {
            cauth::core::session::AuthSession session;
            cauth::steam::auth::set_steam_id(session, begin.steam_id);
            session.account_name =
                poll.account_name.empty() ? request.account_name : poll.account_name;
            session.refresh_token = poll.refresh_token;
            session.access_token = poll.access_token;
            session.session_type = steam_session_type_name(request.platform_type);

            return SteamLoginResult{SteamLoginStatus::Succeeded, std::move(session), "ok"};
        }

        if (attempt + 1 < poll_attempts) {
            if (cancel_requested(options_)) {
                return login_canceled();
            }
            if (begin.interval_seconds > 0.0) {
                if (options_.on_poll_waiting) {
                    options_.on_poll_waiting(attempt + 1, poll_attempts, begin.interval_seconds);
                }
                if (cancel_requested(options_) ||
                    !wait_for_poll_interval(begin.interval_seconds, options_)) {
                    return login_canceled();
                }
            }
        }
    }

    return SteamLoginResult{SteamLoginStatus::SteamGuardRequired, std::nullopt,
                       "Steam authentication did not return a refresh token before polling ended"};
}

} // namespace cauth::steam::auth
