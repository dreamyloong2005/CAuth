#include "steam/auth/steam_auth_application.hpp"

#include "core/auth/jwt.hpp"
#include "steam/auth/steam_session_identity.hpp"
#include "steam/auth/steam_web_api_auth_transport.hpp"
#include "steam/auth/steam_web_cookie_service.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::steam::auth {
namespace {

std::vector<std::string> parse_jwt_audiences(std::string_view token) {
    const auto payload = cauth::core::auth::decode_jwt_payload(token);
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
        if (end != std::string::npos) {
            audiences.emplace_back(payload->substr(start, end - start));
        }
    }
    return audiences;
}

std::optional<std::string> find_json_scalar(std::string_view json, std::string_view key) {
    const auto key_pattern = std::string{"\""} + std::string{key} + "\"";
    const auto key_pos = json.find(key_pattern);
    if (key_pos == std::string_view::npos) return std::nullopt;
    const auto colon_pos = json.find(':', key_pos + key_pattern.size());
    if (colon_pos == std::string_view::npos) return std::nullopt;
    auto value_pos = colon_pos + 1;
    while (value_pos < json.size() &&
           std::isspace(static_cast<unsigned char>(json[value_pos])) != 0) {
        ++value_pos;
    }
    if (value_pos >= json.size()) return std::nullopt;
    if (json[value_pos] == '"') {
        const auto end = json.find('"', value_pos + 1);
        if (end == std::string_view::npos) return std::nullopt;
        return std::string{json.substr(value_pos + 1, end - value_pos - 1)};
    }
    if (json[value_pos] == '[') {
        const auto end = json.find(']', value_pos + 1);
        if (end == std::string_view::npos) return std::nullopt;
        return std::string{json.substr(value_pos, end - value_pos + 1)};
    }
    auto end = value_pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}') ++end;
    while (end > value_pos && std::isspace(static_cast<unsigned char>(json[end - 1])) != 0) --end;
    return std::string{json.substr(value_pos, end - value_pos)};
}

std::optional<SteamLoginPlatformType> steam_login_platform_type_from_session(
    const cauth::core::session::AuthSession& session) {
    if (session.session_type == kSteamSessionTypeMobileApp) {
        return SteamLoginPlatformType::MobileApp;
    }
    if (session.session_type == kSteamSessionTypeWebBrowser) {
        return SteamLoginPlatformType::WebBrowser;
    }
    if (session.session_type == kSteamSessionTypeSteamClient) {
        return SteamLoginPlatformType::SteamClient;
    }
    return std::nullopt;
}

} // namespace

int run_login(cauth::core::session::SessionRepository& store,
              const SteamLoginRequest& request,
              const SteamPlatformLoginOptions& options,
              std::ostream& out,
              std::ostream& err) {
    const auto auth_result = to_core_auth_result(login_with_steam_platform_auth(store, request, options));
    switch (auth_result.status) {
    case cauth::core::auth::AuthStatus::Succeeded:
        out << "Steam login: signed in\n";
        return 0;
    case cauth::core::auth::AuthStatus::AdditionalVerificationRequired:
        out << "Steam login: Steam Guard code required\n";
        return 3;
    case cauth::core::auth::AuthStatus::Unsupported:
        err << "Steam login: " << auth_result.message << '\n';
        return 4;
    case cauth::core::auth::AuthStatus::Failed:
        err << "Steam login failed: " << auth_result.message << '\n';
        return 1;
    }
    return 1;
}

int print_status(cauth::core::session::SessionRepository& store, std::ostream& out) {
    const auto session = store.load_auth_session();
    if (!session.has_value()) {
        out << "Steam auth: not signed in\n";
        return 0;
    }
    out << "Steam auth: signed in as " << cauth::core::session::redacted_account_label(*session) << '\n';
    return 0;
}

int print_whoami(cauth::core::session::SessionRepository& store,
                 std::ostream& out,
                 std::ostream& err) {
    const auto session = store.load_auth_session();
    if (!session.has_value()) {
        err << "Steam auth: not signed in\n";
        return 1;
    }
    out << "Steam account: " << cauth::core::session::redacted_account_label(*session) << '\n';
    out << "SteamID: " << steam_id(*session) << '\n';
    const auto platform_type = steam_login_platform_type_for_refresh_token(*session);
    if (platform_type == SteamLoginPlatformType::WebBrowser) {
        out << "Session type: web-browser\n";
        out << "Access token: not applicable; use auth web-cookies\n";
        return 0;
    }

    SteamWebApiAuthenticationTransport transport;
    const auto token = transport.generate_access_token_for_app(
        SteamGenerateAccessTokenForAppRequest{
            steam_id(*session),
            session->refresh_token,
            platform_type,
        });
    if (!token.result.ok || !token.value.has_value()) {
        out << "Access token: validation failed";
        if (!token.result.error_message.empty()) out << ": " << token.result.error_message;
        out << '\n';
        return 0;
    }
    if (token.value->access_token.empty()) {
        out << "Access token: not returned by Web API for this refresh token type\n";
        return 0;
    }
    out << "Access token: ok\n";
    return 0;
}

SteamLoginPlatformType steam_login_platform_type_for_refresh_token(
    const cauth::core::session::AuthSession& session) {
    if (const auto session_type = steam_login_platform_type_from_session(session);
        session_type.has_value()) {
        return *session_type;
    }

    const auto audiences = parse_jwt_audiences(session.refresh_token);
    for (const auto& candidate : audiences) {
        if (candidate == "mobile") return SteamLoginPlatformType::MobileApp;
        if (candidate == "client") return SteamLoginPlatformType::SteamClient;
    }
    return SteamLoginPlatformType::WebBrowser;
}

int refresh_saved_access_token_from_store(cauth::core::session::SessionRepository& store,
                                          std::ostream& out,
                                          std::ostream& err) {
    auto session = store.load_auth_session();
    if (!session.has_value()) {
        err << "Steam auth: not signed in\n";
        return 1;
    }
    return refresh_saved_access_token(store, *session, out, err) ? 0 : 1;
}

bool refresh_saved_access_token(cauth::core::session::SessionRepository& store,
                                cauth::core::session::AuthSession& session,
                                std::ostream& out,
                                std::ostream& err) {
    const auto platform_type = steam_login_platform_type_for_refresh_token(session);
    if (platform_type == SteamLoginPlatformType::WebBrowser) {
        err << "Access token refresh is not available for web-browser refresh tokens; use auth web-cookies instead\n";
        return false;
    }

    SteamWebApiAuthenticationTransport transport;
    const auto token = transport.generate_access_token_for_app(
        SteamGenerateAccessTokenForAppRequest{
            steam_id(session),
            session.refresh_token,
            platform_type,
        });
    if (!token.result.ok || !token.value.has_value()) {
        err << "Access token refresh failed";
        if (!token.result.error_message.empty()) err << ": " << token.result.error_message;
        err << '\n';
        return false;
    }
    if (token.value->access_token.empty()) {
        err << "Access token refresh failed: Web API did not return an access token\n";
        return false;
    }

    session.access_token = token.value->access_token;
    if (!token.value->refresh_token.empty()) session.refresh_token = token.value->refresh_token;
    store.save_auth_session(session);
    out << "Access token: refreshed\n";
    return true;
}

int print_saved_web_cookies(cauth::core::session::SessionRepository& store,
                            std::ostream& out,
                            std::ostream& err) {
    const auto session = store.load_auth_session();
    if (!session.has_value()) {
        err << "Steam auth: not signed in\n";
        return 1;
    }
    return print_saved_web_cookies(*session, out, err);
}

int print_saved_web_cookies(const cauth::core::session::AuthSession& session,
                            std::ostream& out,
                            std::ostream& err) {
    PlatformSteamHttpRequester requester;
    SteamWebCookieService service{requester};
    const auto result = service.get_web_cookies(session);
    if (!result.ok) {
        err << "Web cookies failed: " << result.error_message << '\n';
        return 1;
    }
    out << "Web cookies: " << result.cookies.size() << '\n';
    if (!result.store_webapi_token.empty()) {
        out << "Store web token: available\n";
    }
    for (const auto& cookie : result.cookies) out << cookie << '\n';
    return 0;
}

int print_token_info(cauth::core::session::SessionRepository& store,
                     std::ostream& out,
                     std::ostream& err) {
    const auto session = store.load_auth_session();
    if (!session.has_value()) {
        err << "Steam auth: not signed in\n";
        return 1;
    }
    if (session->access_token.empty()) {
        err << "Steam auth: access token missing\n";
        return 1;
    }
    const auto payload = cauth::core::auth::decode_jwt_payload(session->access_token);
    if (!payload.has_value()) {
        err << "Access token: not a decodable JWT\n";
        return 1;
    }
    out << "Access token payload:\n";
    for (const auto key : {"aud", "sub", "iss", "exp", "iat"}) {
        const auto value = find_json_scalar(*payload, key);
        if (value.has_value()) out << "  " << key << ": " << *value << '\n';
    }
    return 0;
}

int clear_saved_session(cauth::core::session::SessionRepository& store, std::ostream& out) {
    store.clear_auth_session();
    out << "Steam auth: cleared\n";
    return 0;
}

} // namespace cauth::steam::auth
