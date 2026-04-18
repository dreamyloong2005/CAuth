#ifndef CAUTH_CORE_AUTH_STEAM_AUTH_TRANSPORT_HPP
#define CAUTH_CORE_AUTH_STEAM_AUTH_TRANSPORT_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "steam/auth/login_types.hpp"

namespace cauth::steam::auth {

enum class SteamGuardConfirmationType {
    Unknown,
    EmailCode,
    DeviceCode,
    DeviceConfirmation,
    EmailConfirmation,
};

struct SteamRsaPublicKey {
    std::string modulus_hex;
    std::string exponent_hex;
    std::uint64_t timestamp = 0;
};

struct SteamAllowedConfirmation {
    SteamGuardConfirmationType type = SteamGuardConfirmationType::Unknown;
    std::string message;
};

struct SteamBeginAuthSessionRequest {
    std::string account_name;
    std::string encrypted_password;
    std::uint64_t encryption_timestamp = 0;
    std::string steam_guard_code;
    std::string device_friendly_name = "CAuth";
    bool remember_login = true;
    SteamLoginPlatformType platform_type = SteamLoginPlatformType::SteamClient;
};

struct SteamBeginAuthSessionResponse {
    std::uint64_t client_id = 0;
    std::vector<std::uint8_t> request_id;
    std::uint64_t steam_id = 0;
    double interval_seconds = 0.0;
    std::vector<SteamAllowedConfirmation> allowed_confirmations;
    std::string extended_error_message;
};

struct SteamPollAuthSessionStatusRequest {
    std::uint64_t client_id = 0;
    std::vector<std::uint8_t> request_id;
    SteamLoginPlatformType platform_type = SteamLoginPlatformType::SteamClient;
};

struct SteamPollAuthSessionStatusResponse {
    std::string refresh_token;
    std::string access_token;
    std::string account_name;
    bool had_remote_interaction = false;
};

struct SteamGenerateAccessTokenForAppRequest {
    std::uint64_t steam_id = 0;
    std::string refresh_token;
    SteamLoginPlatformType platform_type = SteamLoginPlatformType::SteamClient;
};

struct SteamGenerateAccessTokenForAppResponse {
    std::string access_token;
    std::string refresh_token;
};

struct SteamTransportResult {
    bool ok = false;
    std::string error_message;
};

template <typename T> struct SteamTransportResponse {
    SteamTransportResult result;
    std::optional<T> value;
};

class SteamAuthenticationTransport {
  public:
    virtual ~SteamAuthenticationTransport() = default;

    virtual SteamTransportResponse<SteamRsaPublicKey>
    get_password_rsa_public_key(const std::string& account_name) = 0;

    virtual SteamTransportResponse<SteamBeginAuthSessionResponse>
    begin_auth_session_via_credentials(const SteamBeginAuthSessionRequest& request) = 0;

    virtual SteamTransportResponse<SteamPollAuthSessionStatusResponse>
    poll_auth_session_status(const SteamPollAuthSessionStatusRequest& request) = 0;

    virtual SteamTransportResponse<SteamGenerateAccessTokenForAppResponse>
    generate_access_token_for_app(const SteamGenerateAccessTokenForAppRequest& request) = 0;
};

} // namespace cauth::steam::auth

#endif
