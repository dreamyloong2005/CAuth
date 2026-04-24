#include "cauth/steam_auth_ffi.h"

#include "core/runtime/android/bridge.hpp"
#include "core/runtime/android/secure_storage_bridge.hpp"
#include "ffi/client_internal.hpp"
#include "steam/auth/cm_authentication_transport.hpp"
#include "steam/auth/login_types.hpp"
#include "steam/auth/platform_auth_runtime.hpp"
#include "steam/auth/steam_login_service.hpp"
#include "steam/auth/steam_network_authenticator.hpp"
#include "steam/auth/steam_password_encryptor.hpp"
#include "steam/auth/steam_session_identity.hpp"
#include "steam/auth/steam_web_api_auth_transport.hpp"
#include "steam/auth/unimplemented_steam_auth_transport.hpp"
#include "steam/cm/cm_logon.hpp"
#include "steam/cm/cm_service_method.hpp"
#include "steam/cm/cm_session.hpp"
#include "steam/cm/steam_directory.hpp"
#include "steam/cm/steam_cm_connector.hpp"
#include "steam/cm/websocket_transport.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string nullable_string(const char* value) {
    return value == nullptr ? std::string{} : std::string{value};
}

cauth::core::platform::RouteSelection from_ffi_route_selection(
    const cauth_route_selection_t* selection) {
    cauth::core::platform::RouteSelection native;
    if (selection == nullptr) {
        return native;
    }
    native.endpoint = nullable_string(selection->endpoint);
    native.protocol = nullable_string(selection->protocol);
    native.role = nullable_string(selection->role);
    return native;
}

std::string base64_encode_bytes(const std::vector<std::uint8_t>& bytes) {
    constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const auto remaining = bytes.size() - index;
        const auto a = bytes[index];
        const auto b = remaining > 1 ? bytes[index + 1] : 0;
        const auto c = remaining > 2 ? bytes[index + 2] : 0;

        encoded.push_back(kAlphabet[(a >> 2) & 0x3f]);
        encoded.push_back(kAlphabet[((a & 0x03) << 4) | ((b >> 4) & 0x0f)]);
        encoded.push_back(remaining > 1 ? kAlphabet[((b & 0x0f) << 2) | ((c >> 6) & 0x03)] : '=');
        encoded.push_back(remaining > 2 ? kAlphabet[c & 0x3f] : '=');
    }

    return encoded;
}

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view encoded) {
    auto decode_char = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+' || ch == '-') return 62;
        if (ch == '/' || ch == '_') return 63;
        return -1;
    };

    if (encoded.empty()) return std::nullopt;

    std::string normalized;
    normalized.reserve(encoded.size() + 4);
    for (const auto ch : encoded) {
        if (ch == '=') {
            normalized.push_back(ch);
            continue;
        }
        if (ch == '-') {
            normalized.push_back('+');
            continue;
        }
        if (ch == '_') {
            normalized.push_back('/');
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) continue;
        normalized.push_back(ch);
    }
    while (normalized.size() % 4 != 0) normalized.push_back('=');

    std::vector<std::uint8_t> bytes;
    bytes.reserve((normalized.size() / 4) * 3);
    for (std::size_t index = 0; index < normalized.size(); index += 4) {
        const auto a = decode_char(normalized[index]);
        const auto b = decode_char(normalized[index + 1]);
        const auto c = normalized[index + 2] == '=' ? -1 : decode_char(normalized[index + 2]);
        const auto d = normalized[index + 3] == '=' ? -1 : decode_char(normalized[index + 3]);

        if (a < 0 || b < 0 || (normalized[index + 2] != '=' && c < 0) ||
            (normalized[index + 3] != '=' && d < 0)) {
            return std::nullopt;
        }

        bytes.push_back(static_cast<std::uint8_t>((a << 2) | (b >> 4)));
        if (normalized[index + 2] != '=') {
            bytes.push_back(static_cast<std::uint8_t>(((b & 0x0f) << 4) | (c >> 2)));
        }
        if (normalized[index + 3] != '=') {
            bytes.push_back(static_cast<std::uint8_t>(((c & 0x03) << 6) | d));
        }
    }
    return bytes;
}

std::optional<std::string> decode_jwt_payload(std::string_view token) {
    const auto first_dot = token.find('.');
    if (first_dot == std::string_view::npos) return std::nullopt;
    const auto second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) return std::nullopt;
    const auto payload = base64_decode(token.substr(first_dot + 1, second_dot - first_dot - 1));
    if (!payload.has_value()) return std::nullopt;
    return std::string{payload->begin(), payload->end()};
}

std::vector<std::string> parse_jwt_audiences(std::string_view token) {
    const auto payload = decode_jwt_payload(token);
    if (!payload.has_value()) return {};
    const auto aud_key = payload->find("\"aud\"");
    if (aud_key == std::string::npos) return {};
    const auto colon = payload->find(':', aud_key + 5);
    if (colon == std::string::npos) return {};

    auto cursor = colon + 1;
    while (cursor < payload->size() &&
           std::isspace(static_cast<unsigned char>((*payload)[cursor])) != 0) {
        ++cursor;
    }
    if (cursor >= payload->size()) return {};

    std::vector<std::string> audiences;
    if ((*payload)[cursor] == '[') {
        ++cursor;
        while (cursor < payload->size()) {
            while (cursor < payload->size() &&
                   std::isspace(static_cast<unsigned char>((*payload)[cursor])) != 0) {
                ++cursor;
            }
            if (cursor >= payload->size() || (*payload)[cursor] == ']') break;
            if ((*payload)[cursor] != '"') break;
            const auto start = ++cursor;
            const auto end = payload->find('"', start);
            if (end == std::string::npos) break;
            audiences.emplace_back(payload->substr(start, end - start));
            cursor = end + 1;
            const auto comma = payload->find_first_of(",]", cursor);
            if (comma == std::string::npos || (*payload)[comma] == ']') break;
            cursor = comma + 1;
        }
        return audiences;
    }

    if ((*payload)[cursor] == '"') {
        const auto start = cursor + 1;
        const auto end = payload->find('"', start);
        if (end != std::string::npos) audiences.emplace_back(payload->substr(start, end - start));
    }
    return audiences;
}

bool has_audience(const std::vector<std::string>& audiences, std::string_view value) {
    for (const auto& candidate : audiences) {
        if (candidate == value) return true;
    }
    return false;
}

cauth::steam::auth::SteamLoginPlatformType to_platform_type(int platform_type) {
    switch (platform_type) {
    case CAUTH_LOGIN_PLATFORM_WEB_BROWSER:
        return cauth::steam::auth::SteamLoginPlatformType::WebBrowser;
    case CAUTH_LOGIN_PLATFORM_MOBILE_APP:
        return cauth::steam::auth::SteamLoginPlatformType::MobileApp;
    case CAUTH_LOGIN_PLATFORM_STEAM_CLIENT:
    default:
        return cauth::steam::auth::SteamLoginPlatformType::SteamClient;
    }
}

bool has_remote_confirmation(
    const cauth::steam::auth::SteamBeginAuthSessionResponse& response) {
    using cauth::steam::auth::SteamGuardConfirmationType;
    for (const auto& confirmation : response.allowed_confirmations) {
        if (confirmation.type == SteamGuardConfirmationType::DeviceConfirmation ||
            confirmation.type == SteamGuardConfirmationType::EmailConfirmation) {
            return true;
        }
    }
    return false;
}

bool has_guard_code_confirmation(
    const cauth::steam::auth::SteamBeginAuthSessionResponse& response) {
    using cauth::steam::auth::SteamGuardConfirmationType;
    for (const auto& confirmation : response.allowed_confirmations) {
        if (confirmation.type == SteamGuardConfirmationType::EmailCode ||
            confirmation.type == SteamGuardConfirmationType::DeviceCode) {
            return true;
        }
    }
    return false;
}

cauth_login_status_t to_cauth_login_status(cauth::core::auth::AuthStatus status) {
    using cauth::core::auth::AuthStatus;
    switch (status) {
    case AuthStatus::Succeeded: return CAUTH_LOGIN_SUCCEEDED;
    case AuthStatus::AdditionalVerificationRequired: return CAUTH_LOGIN_STEAM_GUARD_REQUIRED;
    case AuthStatus::Failed: return CAUTH_LOGIN_FAILED;
    case AuthStatus::Unsupported: return CAUTH_LOGIN_UNSUPPORTED;
    }
    return CAUTH_LOGIN_FAILED;
}

thread_local std::string g_last_login_message;
thread_local std::string g_last_login_module_status;
thread_local std::string g_last_login_account_name;
thread_local std::string g_last_webapi_form_body;
thread_local std::string g_last_webapi_request_id_base64;
thread_local std::string g_last_webapi_rsa_modulus;
thread_local std::string g_last_webapi_rsa_exponent;
thread_local std::string g_last_webapi_error_message;
thread_local std::string g_last_webapi_refresh_token;
thread_local std::string g_last_webapi_access_token;
thread_local std::string g_last_webapi_account_name;
thread_local std::string g_last_cm_probe_endpoint;
thread_local std::string g_last_cm_probe_module_status;
thread_local std::string g_last_cm_probe_status;
thread_local std::string g_last_cm_logon_endpoint;
thread_local std::string g_last_cm_logon_module_status;
thread_local std::string g_last_cm_logon_status;
thread_local std::string g_auth_saved_provider;
thread_local std::string g_auth_saved_subject_id;
thread_local std::string g_cm_route_module_status;
thread_local std::string g_cm_route_backend;
thread_local std::string g_cm_route_message;
thread_local std::vector<std::string> g_cm_route_endpoints;
thread_local std::vector<std::string> g_cm_route_protocols;
thread_local std::vector<std::string> g_cm_route_roles;
thread_local std::vector<std::string> g_cm_route_notes;
thread_local std::vector<cauth_route_probe_entry_t> g_cm_route_entries;

} // namespace

cauth_result_t cauth_auth_login_password(cauth_client_t* client,
                                         const cauth_login_request_t* request,
                                         cauth_login_result_t* out_result) {
    if (client == nullptr || request == nullptr || out_result == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_result->status = CAUTH_LOGIN_FAILED;
    out_result->result = CAUTH_ERROR_INTERNAL;
    out_result->module_status = "";
    out_result->message = "";
    out_result->steam_id = 0;
    out_result->account_name = "";

    try {
        cauth::steam::auth::SteamLoginRequest login_request;
        login_request.account_name = nullable_string(request->account_name);
        login_request.password = nullable_string(request->password);
        login_request.steam_guard_code = nullable_string(request->steam_guard_code);
        login_request.device_name = nullable_string(request->device_name);
        login_request.remember_session = request->remember_session != 0;
        login_request.platform_type = to_platform_type(request->platform_type);
        login_request.route_selection = from_ffi_route_selection(&request->route_selection);

        const auto login_result =
            cauth::steam::auth::login_with_steam_platform_auth(*client->session_repository, login_request);

        g_last_login_message = login_result.message;
        g_last_login_module_status =
            login_result.status == cauth::steam::auth::SteamLoginStatus::Succeeded
                ? "succeeded"
                : login_result.status ==
                          cauth::steam::auth::SteamLoginStatus::SteamGuardRequired
                      ? "action_required"
                      : login_result.status == cauth::steam::auth::SteamLoginStatus::Unsupported
                            ? "unsupported"
                            : "failed";
        g_last_login_account_name =
            login_result.session.has_value() ? login_result.session->account_name : "";

        out_result->status =
            to_cauth_login_status(cauth::steam::auth::to_core_auth_status(login_result.status));
        out_result->result = CAUTH_OK;
        out_result->module_status = g_last_login_module_status.c_str();
        out_result->message = g_last_login_message.c_str();
        out_result->steam_id =
            login_result.session.has_value() ? cauth::steam::auth::steam_id(*login_result.session)
                                             : 0;
        out_result->account_name = g_last_login_account_name.c_str();
        return out_result->result;
    } catch (const std::bad_alloc&) {
        out_result->status = CAUTH_LOGIN_FAILED;
        out_result->result = CAUTH_ERROR_OUT_OF_MEMORY;
        out_result->module_status = "failed";
        out_result->message = "out of memory";
        return CAUTH_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        out_result->status = CAUTH_LOGIN_FAILED;
        out_result->result = CAUTH_ERROR_INTERNAL;
        out_result->module_status = "failed";
        out_result->message = "internal error";
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_auth_get_saved_session(cauth_client_t* client,
                                            unsigned long long steam_id,
                                            cauth_saved_session_t* out_session) {
    if (client == nullptr || steam_id == 0 || out_session == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_session->present = 0;
    out_session->steam_id = 0;
    out_session->account_name = "";
    out_session->has_refresh_token = 0;
    out_session->has_access_token = 0;
    out_session->created_at_unix_seconds = 0;

    cauth_session_record_t generic_session{};
    const auto subject_id = std::to_string(steam_id);
    const auto result = cauth_session_get_saved(
        client,
        cauth::steam::auth::kSteamAuthProvider.data(),
        subject_id.c_str(),
        &generic_session);
    if (result != CAUTH_OK || generic_session.present == 0) {
        return result;
    }

    g_auth_saved_provider = generic_session.provider == nullptr ? "" : generic_session.provider;
    g_auth_saved_subject_id = generic_session.subject_id == nullptr ? "" : generic_session.subject_id;
    out_session->present = generic_session.present;
    out_session->steam_id = 0;
    if (g_auth_saved_provider == cauth::steam::auth::kSteamAuthProvider) {
        out_session->steam_id = std::strtoull(g_auth_saved_subject_id.c_str(), nullptr, 10);
    }
    out_session->account_name = generic_session.account_name;
    out_session->has_refresh_token = generic_session.has_refresh_token;
    out_session->has_access_token = generic_session.has_access_token;
    out_session->created_at_unix_seconds = generic_session.created_at_unix_seconds;
    return CAUTH_OK;
}

cauth_result_t cauth_auth_clear_saved_session(cauth_client_t* client,
                                              unsigned long long steam_id) {
    if (steam_id == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }
    const auto subject_id = std::to_string(steam_id);
    return cauth_session_clear_account(
        client,
        cauth::steam::auth::kSteamAuthProvider.data(),
        subject_id.c_str());
}

cauth_result_t cauth_auth_save_session(cauth_client_t* client,
                                       const cauth_auth_session_t* session) {
    if (client == nullptr || session == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    const auto provider = std::string{cauth::steam::auth::kSteamAuthProvider};
    const auto subject_id = std::to_string(session->steam_id);
    cauth_session_record_t generic_session{};
    generic_session.present = 1;
    generic_session.provider = provider.c_str();
    generic_session.subject_id = subject_id.c_str();
    generic_session.account_name = session->account_name;
    generic_session.refresh_token = session->refresh_token;
    return cauth_session_save(client, &generic_session);
}

cauth_result_t cauth_auth_parse_password_rsa_response(const char* json,
                                                      cauth_webapi_rsa_key_t* out_key) {
    if (json == nullptr || out_key == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_key->present = 0;
    out_key->modulus_hex = "";
    out_key->exponent_hex = "";
    out_key->timestamp = 0;

    try {
        const auto parsed =
            cauth::steam::auth::parse_get_password_rsa_public_key_response(json);
        if (!parsed.has_value()) return CAUTH_ERROR_INTERNAL;

        g_last_webapi_rsa_modulus = parsed->modulus_hex;
        g_last_webapi_rsa_exponent = parsed->exponent_hex;
        out_key->present = 1;
        out_key->modulus_hex = g_last_webapi_rsa_modulus.c_str();
        out_key->exponent_hex = g_last_webapi_rsa_exponent.c_str();
        out_key->timestamp = parsed->timestamp;
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_auth_build_begin_session_form_body(
    const cauth_webapi_begin_session_request_t* request,
    const char** out_form_body) {
    if (request == nullptr || out_form_body == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        cauth::steam::auth::SteamBeginAuthSessionRequest native_request;
        native_request.account_name = nullable_string(request->account_name);
        native_request.encrypted_password = nullable_string(request->encrypted_password);
        native_request.encryption_timestamp = request->encryption_timestamp;
        native_request.steam_guard_code = nullable_string(request->steam_guard_code);
        native_request.device_friendly_name = nullable_string(request->device_name);
        native_request.remember_login = request->remember_login != 0;
        native_request.platform_type = to_platform_type(request->platform_type);

        const auto protobuf =
            cauth::steam::auth::encode_begin_auth_session_request(native_request);
        g_last_webapi_form_body =
            "input_protobuf_encoded=" +
            cauth::steam::auth::steam_web_api_url_encode(base64_encode_bytes(protobuf));
        *out_form_body = g_last_webapi_form_body.c_str();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_auth_parse_begin_session_response(
    const void* bytes,
    unsigned long long size,
    cauth_webapi_begin_session_response_t* out_response) {
    if (bytes == nullptr || out_response == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->client_id = 0;
    out_response->request_id_base64 = "";
    out_response->steam_id = 0;
    out_response->interval_seconds = 0.0;
    out_response->confirmation_count = 0;
    out_response->has_remote_confirmation = 0;
    out_response->guard_code_allowed = 0;
    out_response->error_message = "";

    try {
        const auto view = std::string_view{static_cast<const char*>(bytes),
                                           static_cast<std::size_t>(size)};
        const auto parsed = cauth::steam::auth::parse_begin_auth_session_response(view);
        if (!parsed.has_value()) return CAUTH_ERROR_INTERNAL;

        g_last_webapi_request_id_base64 = base64_encode_bytes(parsed->request_id);
        g_last_webapi_error_message = parsed->extended_error_message;

        out_response->present = 1;
        out_response->client_id = parsed->client_id;
        out_response->request_id_base64 = g_last_webapi_request_id_base64.c_str();
        out_response->steam_id = parsed->steam_id;
        out_response->interval_seconds = parsed->interval_seconds;
        out_response->confirmation_count =
            static_cast<int>(parsed->allowed_confirmations.size());
        out_response->has_remote_confirmation = has_remote_confirmation(*parsed) ? 1 : 0;
        out_response->guard_code_allowed = has_guard_code_confirmation(*parsed) ? 1 : 0;
        out_response->error_message = g_last_webapi_error_message.c_str();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_auth_build_poll_session_form_body(
    const cauth_webapi_poll_session_request_t* request,
    const char** out_form_body) {
    if (request == nullptr || out_form_body == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        const auto request_id = base64_decode(nullable_string(request->request_id_base64));
        if (!request_id.has_value()) return CAUTH_ERROR_INVALID_ARGUMENT;

        cauth::steam::auth::SteamPollAuthSessionStatusRequest native_request;
        native_request.client_id = request->client_id;
        native_request.request_id = *request_id;

        const auto protobuf =
            cauth::steam::auth::encode_poll_auth_session_status_request(native_request);
        g_last_webapi_form_body =
            "input_protobuf_encoded=" +
            cauth::steam::auth::steam_web_api_url_encode(base64_encode_bytes(protobuf));
        *out_form_body = g_last_webapi_form_body.c_str();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_auth_parse_poll_session_response(
    const void* bytes,
    unsigned long long size,
    cauth_webapi_poll_session_response_t* out_response) {
    if (bytes == nullptr || out_response == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->refresh_token = "";
    out_response->access_token = "";
    out_response->account_name = "";
    out_response->had_remote_interaction = 0;

    try {
        const auto view = std::string_view{static_cast<const char*>(bytes),
                                           static_cast<std::size_t>(size)};
        const auto parsed = cauth::steam::auth::parse_poll_auth_session_status_response(view);
        if (!parsed.has_value()) return CAUTH_ERROR_INTERNAL;

        g_last_webapi_refresh_token = parsed->refresh_token;
        g_last_webapi_access_token = parsed->access_token;
        g_last_webapi_account_name = parsed->account_name;

        out_response->present = 1;
        out_response->refresh_token = g_last_webapi_refresh_token.c_str();
        out_response->access_token = g_last_webapi_access_token.c_str();
        out_response->account_name = g_last_webapi_account_name.c_str();
        out_response->had_remote_interaction = parsed->had_remote_interaction ? 1 : 0;
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_auth_build_generate_access_token_form_body(
    const cauth_webapi_generate_access_token_request_t* request,
    const char** out_form_body) {
    if (request == nullptr || out_form_body == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        cauth::steam::auth::SteamGenerateAccessTokenForAppRequest native_request;
        native_request.steam_id = request->steam_id;
        native_request.refresh_token = nullable_string(request->refresh_token);

        const auto protobuf =
            cauth::steam::auth::encode_generate_access_token_for_app_request(native_request);
        g_last_webapi_form_body =
            "input_protobuf_encoded=" +
            cauth::steam::auth::steam_web_api_url_encode(base64_encode_bytes(protobuf));
        *out_form_body = g_last_webapi_form_body.c_str();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_auth_parse_generate_access_token_response(
    const void* bytes,
    unsigned long long size,
    cauth_webapi_generate_access_token_response_t* out_response) {
    if (bytes == nullptr || out_response == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->present = 0;
    out_response->access_token = "";
    out_response->refresh_token = "";

    try {
        const auto view = std::string_view{static_cast<const char*>(bytes),
                                           static_cast<std::size_t>(size)};
        const auto parsed =
            cauth::steam::auth::parse_generate_access_token_for_app_response(view);
        if (!parsed.has_value()) return CAUTH_ERROR_INTERNAL;

        g_last_webapi_access_token = parsed->access_token;
        g_last_webapi_refresh_token = parsed->refresh_token;
        out_response->present = 1;
        out_response->access_token = g_last_webapi_access_token.c_str();
        out_response->refresh_token = g_last_webapi_refresh_token.c_str();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_probe_app_id(unsigned long long app_id, cauth_app_id_probe_t* out_probe) {
    if (out_probe == nullptr) return CAUTH_ERROR_INVALID_ARGUMENT;

    out_probe->app_id = app_id;
    out_probe->default_branch = "public";

    if (app_id == 0 || app_id > std::numeric_limits<std::uint32_t>::max()) {
        out_probe->valid = 0;
        out_probe->status = "invalid app id";
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_probe->valid = 1;
    out_probe->status =
        "app id accepted; Android Steam network lookup is not implemented in this build yet";
    return CAUTH_OK;
}

cauth_result_t cauth_get_capabilities(cauth_capabilities_t* out_capabilities) {
    if (out_capabilities == nullptr) return CAUTH_ERROR_INVALID_ARGUMENT;

#ifdef _WIN32
    out_capabilities->web_api_auth_transport = 1;
    out_capabilities->cm_websocket_transport = 1;
    out_capabilities->password_rsa_encryptor = 1;
    out_capabilities->depot_content_decrypt = 1;
    out_capabilities->android_secure_store_bridge = 0;
#elif defined(__ANDROID__)
    out_capabilities->web_api_auth_transport = 0;
    out_capabilities->cm_websocket_transport =
        cauth::core::runtime::is_android_platform_bridge_available() ? 1 : 0;
    out_capabilities->password_rsa_encryptor = 0;
    out_capabilities->depot_content_decrypt = 0;
    out_capabilities->android_secure_store_bridge =
        cauth::core::runtime::is_android_secure_storage_bridge_available() ? 1 : 0;
#else
    out_capabilities->web_api_auth_transport = 0;
    out_capabilities->cm_websocket_transport = 0;
    out_capabilities->password_rsa_encryptor = 0;
    out_capabilities->depot_content_decrypt = 0;
    out_capabilities->android_secure_store_bridge = 0;
#endif

    return CAUTH_OK;
}

cauth_result_t cauth_cm_probe_on_route(const cauth_route_selection_t* route_selection,
                                       cauth_cm_probe_result_t* out_probe) {
    if (out_probe == nullptr) return CAUTH_ERROR_INVALID_ARGUMENT;

    out_probe->ok = 0;
    out_probe->endpoint = "";
    out_probe->module_status = "";
    out_probe->status = "";

    try {
        const auto native_route_selection = from_ffi_route_selection(route_selection);
        const auto report = cauth::core::cm::probe_websocket_routes(
            5,
            nullptr,
            native_route_selection.empty() ? nullptr : &native_route_selection);
        g_last_cm_probe_endpoint =
            report.routes.empty()
                ? std::string{}
                : report.routes.front().endpoint.address + ":" +
                      std::to_string(report.routes.front().endpoint.port);
        g_last_cm_probe_module_status = report.ok ? "connected" : report.module_status;
        g_last_cm_probe_status = report.message.empty()
                                     ? (report.ok ? "connected"
                                                  : "CM websocket probe failed for all endpoints")
                                     : report.message;
        out_probe->ok = report.ok ? 1 : 0;
        out_probe->endpoint = g_last_cm_probe_endpoint.c_str();
        out_probe->module_status = g_last_cm_probe_module_status.c_str();
        out_probe->status = g_last_cm_probe_status.c_str();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_cm_probe(cauth_cm_probe_result_t* out_probe) {
    return cauth_cm_probe_on_route(nullptr, out_probe);
}

cauth_result_t cauth_cm_logon_on_route(cauth_client_t* client,
                                       unsigned long long steam_id,
                                       const cauth_route_selection_t* route_selection,
                                       cauth_cm_logon_result_t* out_result) {
    if (client == nullptr || steam_id == 0 || out_result == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_result->ok = 0;
    out_result->endpoint = "";
    out_result->module_status = "";
    out_result->status = "";
    out_result->eresult = 0;
    out_result->eresult_extended = 0;
    out_result->heartbeat_seconds = 0;
    out_result->steam_id = 0;

    try {
        const auto session = client->session_repository->load_auth_session(
            cauth::steam::auth::kSteamAuthProvider,
            std::to_string(steam_id));
        if (!session.has_value()) {
            g_last_cm_logon_endpoint.clear();
            g_last_cm_logon_module_status = "failed";
            g_last_cm_logon_status = "Auth session: not signed in";
            out_result->module_status = g_last_cm_logon_module_status.c_str();
            out_result->status = g_last_cm_logon_status.c_str();
            return CAUTH_OK;
        }
        if (session->refresh_token.empty()) {
            g_last_cm_logon_endpoint.clear();
            g_last_cm_logon_module_status = "failed";
            g_last_cm_logon_status =
                "Auth session: refresh token missing; please log in again with the current build";
            out_result->module_status = g_last_cm_logon_module_status.c_str();
            out_result->status = g_last_cm_logon_status.c_str();
            out_result->steam_id = cauth::steam::auth::steam_id(*session);
            return CAUTH_OK;
        }

        const auto audiences = parse_jwt_audiences(session->access_token);
        if (!audiences.empty() && !has_audience(audiences, "client") &&
            has_audience(audiences, "web")) {
            g_last_cm_logon_endpoint.clear();
            g_last_cm_logon_module_status = "failed";
            g_last_cm_logon_status =
                "Saved session looks web-only (aud=web); CM logon requires a client-capable session";
            out_result->module_status = g_last_cm_logon_module_status.c_str();
            out_result->status = g_last_cm_logon_status.c_str();
            out_result->steam_id = cauth::steam::auth::steam_id(*session);
            return CAUTH_OK;
        }

        const auto native_route_selection = from_ffi_route_selection(route_selection);
        cauth::core::cm::SteamCmConnector connector;
        const auto logon_result = connector.with_logged_on_session(
            *session,
            5,
            native_route_selection.empty() ? nullptr : &native_route_selection,
            [&](const cauth::core::cm::CmServerEndpoint& endpoint, cauth::core::cm::CmSession& cm_session) {
                (void)cm_session;
                g_last_cm_logon_endpoint = endpoint.address + ":" + std::to_string(endpoint.port);
                g_last_cm_logon_status = "logged on";
                g_last_cm_logon_module_status = "succeeded";
                out_result->ok = 1;
                out_result->endpoint = g_last_cm_logon_endpoint.c_str();
                out_result->module_status = g_last_cm_logon_module_status.c_str();
                out_result->status = g_last_cm_logon_status.c_str();
                out_result->eresult = 1;
                out_result->eresult_extended = 1;
                out_result->heartbeat_seconds = 0;
                out_result->steam_id = cauth::steam::auth::steam_id(*session);
                return cauth::core::cm::SteamCmAttemptResult{
                    cauth::core::cm::SteamCmContinuation::Stop,
                    true,
                    "",
                };
            });

        if (logon_result.ok) {
            return CAUTH_OK;
        }

        g_last_cm_logon_module_status = "failed";
        g_last_cm_logon_status = logon_result.error_message.empty()
                                     ? "CM logon failed for all endpoints"
                                     : logon_result.error_message;
        out_result->endpoint = g_last_cm_logon_endpoint.c_str();
        out_result->module_status = g_last_cm_logon_module_status.c_str();
        out_result->status = g_last_cm_logon_status.c_str();
        out_result->steam_id = cauth::steam::auth::steam_id(*session);
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_cm_logon(cauth_client_t* client,
                              unsigned long long steam_id,
                              cauth_cm_logon_result_t* out_result) {
    return cauth_cm_logon_on_route(client, steam_id, nullptr, out_result);
}

cauth_result_t cauth_auth_probe_cm_routes(unsigned int max_count,
                                          cauth_route_probe_result_t* out_result) {
    if (out_result == nullptr || max_count == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_result->ok = 0;
    out_result->module_status = "";
    out_result->backend = "";
    out_result->message = "";
    out_result->route_count = 0;
    out_result->routes = nullptr;

    try {
        const auto report = cauth::core::cm::probe_websocket_routes(max_count);
        g_cm_route_module_status = report.module_status;
        g_cm_route_backend = "cm";
        g_cm_route_message = report.message;
        g_cm_route_endpoints.clear();
        g_cm_route_protocols.clear();
        g_cm_route_roles.clear();
        g_cm_route_notes.clear();
        g_cm_route_entries.clear();
        g_cm_route_endpoints.reserve(report.routes.size());
        g_cm_route_protocols.reserve(report.routes.size());
        g_cm_route_roles.reserve(report.routes.size());
        g_cm_route_notes.reserve(report.routes.size());
        for (const auto& route : report.routes) {
            g_cm_route_endpoints.push_back(route.route.endpoint);
            g_cm_route_protocols.push_back(route.route.protocol);
            g_cm_route_roles.push_back(route.route.role);
            g_cm_route_notes.push_back(route.route.note);
        }
        g_cm_route_entries.reserve(report.routes.size());
        for (std::size_t index = 0; index < report.routes.size(); ++index) {
            const auto& route = report.routes[index].route;
            g_cm_route_entries.push_back(cauth_route_probe_entry_t{
                g_cm_route_endpoints[index].c_str(),
                g_cm_route_protocols[index].c_str(),
                g_cm_route_roles[index].c_str(),
                g_cm_route_notes[index].c_str(),
                route.latency_ms,
                route.latency_known ? 1 : 0,
                route.recent_success ? 1 : 0,
                route.recent_failure ? 1 : 0,
                route.success_count,
                route.failure_count,
            });
        }

        out_result->ok = report.ok ? 1 : 0;
        out_result->module_status = g_cm_route_module_status.c_str();
        out_result->backend = g_cm_route_backend.c_str();
        out_result->message = g_cm_route_message.c_str();
        out_result->route_count = static_cast<unsigned long long>(g_cm_route_entries.size());
        out_result->routes = g_cm_route_entries.empty() ? nullptr : g_cm_route_entries.data();
        return CAUTH_OK;
    } catch (const std::bad_alloc&) {
        return CAUTH_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}
