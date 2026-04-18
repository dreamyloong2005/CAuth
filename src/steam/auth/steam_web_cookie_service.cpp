#include "steam/auth/steam_web_cookie_service.hpp"

#include "steam/auth/steam_session_identity.hpp"
#include "steam/auth/steam_web_api_auth_transport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string_view>

namespace cauth::steam::auth {
namespace {

constexpr std::string_view kFinalizeLoginUrl = "https://login.steampowered.com/jwt/finalizelogin";
constexpr std::string_view kAjaxRefreshUrl = "https://login.steampowered.com/jwt/ajaxrefresh";
constexpr std::string_view kSteamCommunityOrigin = "https://steamcommunity.com";
constexpr std::string_view kFinalizeLoginRedirect = "https://steamcommunity.com/login/home/?goto=";
constexpr std::string_view kSteamStoreBaseUrl = "https://store.steampowered.com";
constexpr std::string_view kDefaultBrowserUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

struct FinalizeTransfer {
    std::string url;
    std::vector<std::pair<std::string, std::string>> params;
};

struct FinalizeLoginResponse {
    std::vector<FinalizeTransfer> transfers;
};

struct StorePageState {
    std::vector<std::string> cookies;
    std::string session_id;
    std::string webapi_token;
};

struct AjaxRefreshResponse {
    std::string login_url;
    std::vector<std::pair<std::string, std::string>> fields;
};

struct SetTokenResponse {
    std::string token;
};

bool ascii_iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

std::string random_session_id() {
    std::array<unsigned char, 12> bytes{};
    std::random_device random_device;
    for (auto& byte : bytes) {
        byte = static_cast<unsigned char>(random_device());
    }

    std::ostringstream out;
    out << std::hex;
    for (const auto byte : bytes) {
        out.width(2);
        out.fill('0');
        out << static_cast<unsigned int>(byte);
    }
    return out.str();
}

std::string trim_ascii(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string{value.substr(start, end - start)};
}

std::string json_unescape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto ch = value[index];
        if (ch != '\\' || index + 1 >= value.size()) {
            result.push_back(ch);
            continue;
        }

        const auto escaped = value[++index];
        switch (escaped) {
        case '"':
        case '\\':
        case '/':
            result.push_back(escaped);
            break;
        case 'b':
            result.push_back('\b');
            break;
        case 'f':
            result.push_back('\f');
            break;
        case 'n':
            result.push_back('\n');
            break;
        case 'r':
            result.push_back('\r');
            break;
        case 't':
            result.push_back('\t');
            break;
        default:
            result.push_back(escaped);
            break;
        }
    }
    return result;
}

std::string_view find_json_string_value(std::string_view json, std::string_view key,
                                        std::size_t search_from = 0) {
    const auto needle = "\"" + std::string{key} + "\"";
    const auto key_position = json.find(needle, search_from);
    if (key_position == std::string_view::npos) {
        return {};
    }

    auto colon = json.find(':', key_position + needle.size());
    if (colon == std::string_view::npos) {
        return {};
    }
    auto quote = json.find('"', colon + 1);
    if (quote == std::string_view::npos) {
        return {};
    }

    const auto start = quote + 1;
    bool escaped = false;
    for (auto index = start; index < json.size(); ++index) {
        const auto ch = json[index];
        if (!escaped && ch == '"') {
            return json.substr(start, index - start);
        }
        escaped = (!escaped && ch == '\\');
        if (ch != '\\') {
            escaped = false;
        }
    }

    return {};
}

std::size_t skip_json_string(std::string_view json, std::size_t offset) {
    bool escaped = false;
    for (std::size_t index = offset + 1; index < json.size(); ++index) {
        const auto ch = json[index];
        if (!escaped && ch == '"') {
            return index + 1;
        }
        escaped = (!escaped && ch == '\\');
        if (ch != '\\') {
            escaped = false;
        }
    }
    return std::string_view::npos;
}

std::size_t find_matching_brace(std::string_view json, std::size_t start,
                                char open_char, char close_char) {
    int depth = 0;
    for (std::size_t index = start; index < json.size(); ++index) {
        if (json[index] == '"') {
            index = skip_json_string(json, index);
            if (index == std::string_view::npos) {
                return std::string_view::npos;
            }
            --index;
            continue;
        }

        if (json[index] == open_char) {
            ++depth;
        } else if (json[index] == close_char) {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    return std::string_view::npos;
}

std::vector<std::pair<std::string, std::string>> parse_string_map_object(std::string_view json) {
    std::vector<std::pair<std::string, std::string>> values;
    std::size_t offset = 0;
    while (offset < json.size()) {
        const auto key_start = json.find('"', offset);
        if (key_start == std::string_view::npos) {
            break;
        }
        const auto key_end = skip_json_string(json, key_start);
        if (key_end == std::string_view::npos || key_end <= key_start + 1) {
            break;
        }
        const std::string key{json.substr(key_start + 1, key_end - key_start - 2)};
        const auto colon = json.find(':', key_end);
        if (colon == std::string_view::npos) {
            break;
        }
        auto value_start = colon + 1;
        while (value_start < json.size() &&
               std::isspace(static_cast<unsigned char>(json[value_start])) != 0) {
            ++value_start;
        }
        if (value_start >= json.size()) {
            break;
        }

        std::string value;
        if (json[value_start] == '"') {
            const auto value_end = skip_json_string(json, value_start);
            if (value_end == std::string_view::npos || value_end <= value_start + 1) {
                break;
            }
            value = json_unescape(json.substr(value_start + 1, value_end - value_start - 2));
            offset = value_end;
        } else {
            auto value_end = value_start;
            while (value_end < json.size() && json[value_end] != ',' && json[value_end] != '}') {
                ++value_end;
            }
            value = trim_ascii(json.substr(value_start, value_end - value_start));
            offset = value_end;
        }

        if (!key.empty() && !value.empty()) {
            values.emplace_back(key, value);
        }
    }
    return values;
}

std::optional<std::string> find_json_string(std::string_view json, std::string_view key) {
    const auto value = find_json_string_value(json, key);
    if (value.empty()) {
        return std::nullopt;
    }
    return json_unescape(value);
}

std::optional<bool> find_json_bool(std::string_view json, std::string_view key) {
    const auto needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    ++position;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position])) != 0) {
        ++position;
    }
    if (json.substr(position, 4) == "true") {
        return true;
    }
    if (json.substr(position, 5) == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<FinalizeLoginResponse> parse_finalize_login_response(std::string_view json) {
    FinalizeLoginResponse response;
    const auto transfer_key = json.find("\"transfer_info\"");
    if (transfer_key != std::string_view::npos) {
        const auto array_start = json.find('[', transfer_key);
        if (array_start == std::string_view::npos) {
            return std::nullopt;
        }
        const auto array_end = find_matching_brace(json, array_start, '[', ']');
        if (array_end == std::string_view::npos) {
            return std::nullopt;
        }

        std::size_t offset = array_start + 1;
        while (offset < array_end) {
            const auto object_start = json.find('{', offset);
            if (object_start == std::string_view::npos || object_start >= array_end) {
                break;
            }
            const auto object_end = find_matching_brace(json, object_start, '{', '}');
            if (object_end == std::string_view::npos || object_end > array_end) {
                return std::nullopt;
            }

            const auto object = json.substr(object_start, object_end - object_start + 1);
            FinalizeTransfer transfer;
            const auto url = find_json_string_value(object, "url");
            if (url.empty()) {
                return std::nullopt;
            }
            transfer.url = json_unescape(url);

            const auto params_key = object.find("\"params\"");
            if (params_key == std::string_view::npos) {
                return std::nullopt;
            }
            const auto params_start = object.find('{', params_key);
            if (params_start == std::string_view::npos) {
                return std::nullopt;
            }
            const auto params_end = find_matching_brace(object, params_start, '{', '}');
            if (params_end == std::string_view::npos) {
                return std::nullopt;
            }
            transfer.params = parse_string_map_object(
                object.substr(params_start + 1, params_end - params_start - 1));
            response.transfers.push_back(std::move(transfer));
            offset = object_end + 1;
        }
    } else {
        const auto urls_key = json.find("\"transfer_urls\"");
        const auto parameters_key = json.find("\"transfer_parameters\"");
        if (urls_key == std::string_view::npos || parameters_key == std::string_view::npos) {
            return std::nullopt;
        }

        const auto urls_start = json.find('[', urls_key);
        if (urls_start == std::string_view::npos) {
            return std::nullopt;
        }
        const auto urls_end = find_matching_brace(json, urls_start, '[', ']');
        if (urls_end == std::string_view::npos) {
            return std::nullopt;
        }

        const auto params_start = json.find('{', parameters_key);
        if (params_start == std::string_view::npos) {
            return std::nullopt;
        }
        const auto params_end = find_matching_brace(json, params_start, '{', '}');
        if (params_end == std::string_view::npos) {
            return std::nullopt;
        }
        const auto shared_params =
            parse_string_map_object(json.substr(params_start + 1, params_end - params_start - 1));

        std::size_t offset = urls_start + 1;
        while (offset < urls_end) {
            const auto quote_start = json.find('"', offset);
            if (quote_start == std::string_view::npos || quote_start >= urls_end) {
                break;
            }
            const auto quote_end = skip_json_string(json, quote_start);
            if (quote_end == std::string_view::npos || quote_end <= quote_start + 1) {
                return std::nullopt;
            }

            FinalizeTransfer transfer;
            transfer.url =
                json_unescape(json.substr(quote_start + 1, quote_end - quote_start - 2));
            transfer.params = shared_params;
            response.transfers.push_back(std::move(transfer));
            offset = quote_end;
        }
    }

    if (response.transfers.empty()) {
        return std::nullopt;
    }
    return response;
}

std::string host_from_url(std::string_view url) {
    const auto scheme_pos = url.find("://");
    const auto host_start = scheme_pos == std::string_view::npos ? 0 : scheme_pos + 3;
    const auto path_start = url.find('/', host_start);
    return std::string{url.substr(
        host_start, path_start == std::string_view::npos ? url.size() - host_start
                                                         : path_start - host_start)};
}

std::string maybe_attach_domain(std::string_view cookie, std::string_view domain) {
    if (cookie.find("Domain=") != std::string_view::npos ||
        cookie.find("domain=") != std::string_view::npos) {
        return trim_ascii(cookie);
    }
    return trim_ascii(cookie) + "; Domain=" + std::string{domain};
}

std::vector<std::uint8_t> build_multipart_form_data(
    const std::vector<std::pair<std::string, std::string>>& fields, const std::string& boundary) {
    std::vector<std::uint8_t> body;
    auto append_text = [&body](std::string_view text) {
        body.insert(body.end(), text.begin(), text.end());
    };

    for (const auto& [name, value] : fields) {
        append_text("--");
        append_text(boundary);
        append_text("\r\nContent-Disposition: form-data; name=\"");
        append_text(name);
        append_text("\"\r\n\r\n");
        append_text(value);
        append_text("\r\n");
    }

    append_text("--");
    append_text(boundary);
    append_text("--\r\n");
    return body;
}

cauth::core::platform::HttpRequest make_multipart_request(
    std::string_view url, const std::vector<std::pair<std::string, std::string>>& fields,
    std::vector<cauth::core::platform::HttpHeader> headers = {}) {
    constexpr std::string_view kBoundary = "----CAuthSteamWebCookieBoundary";

    cauth::core::platform::HttpRequest request;
    request.method = cauth::core::platform::HttpMethod::Post;
    request.url = std::string{url};
    request.content_type = "multipart/form-data; boundary=" + std::string{kBoundary};
    request.body = build_multipart_form_data(fields, std::string{kBoundary});
    request.headers = std::move(headers);
    return request;
}

cauth::core::platform::HttpRequest make_form_request(
    std::string_view url, const std::vector<std::pair<std::string, std::string>>& fields,
    std::vector<cauth::core::platform::HttpHeader> headers = {}) {
    std::string body;
    bool first = true;
    for (const auto& [name, value] : fields) {
        if (!first) {
            body += '&';
        }
        first = false;
        body += steam_web_api_url_encode(name);
        body += '=';
        body += steam_web_api_url_encode(value);
    }

    cauth::core::platform::HttpRequest request;
    request.method = cauth::core::platform::HttpMethod::Post;
    request.url = std::string{url};
    request.content_type = "application/x-www-form-urlencoded";
    request.body.assign(body.begin(), body.end());
    request.headers = std::move(headers);
    return request;
}

std::vector<std::string> collect_response_cookies(
    const cauth::core::platform::HttpResponse& response, std::string_view fallback_domain) {
    std::vector<std::string> cookies;
    for (const auto& cookie :
         cauth::core::platform::http_header_values(response, "Set-Cookie")) {
        cookies.push_back(maybe_attach_domain(cookie, fallback_domain));
    }
    return cookies;
}

void merge_cookie_vectors(std::vector<std::string>& destination,
                          const std::vector<std::string>& cookies) {
    for (const auto& cookie : cookies) {
        const auto pair_end = cookie.find(';');
        const auto pair = trim_ascii(cookie.substr(0, pair_end));
        const auto equals = pair.find('=');
        if (equals == std::string::npos || equals == 0) {
            continue;
        }
        const auto name = trim_ascii(pair.substr(0, equals));

        auto replaced = false;
        for (auto& existing : destination) {
            const auto existing_pair_end = existing.find(';');
            const auto existing_pair = trim_ascii(existing.substr(0, existing_pair_end));
            const auto existing_equals = existing_pair.find('=');
            if (existing_equals == std::string::npos || existing_equals == 0) {
                continue;
            }
            if (!ascii_iequals(trim_ascii(existing_pair.substr(0, existing_equals)), name)) {
                continue;
            }

            const auto existing_domain_pos = existing.find("Domain=");
            const auto cookie_domain_pos = cookie.find("Domain=");
            if (existing_domain_pos == std::string::npos || cookie_domain_pos == std::string::npos ||
                ascii_iequals(existing.substr(existing_domain_pos), cookie.substr(cookie_domain_pos))) {
                existing = cookie;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            destination.push_back(cookie);
        }
    }
}

std::optional<StorePageState> fetch_store_page_state(
    SteamHttpRequester& requester,
    std::vector<std::string>& cookies,
    std::string_view store_path) {
    const auto store_cookie_header =
        build_cookie_request_header(cookies, "store.steampowered.com");
    if (store_cookie_header.empty()) {
        return std::nullopt;
    }

    cauth::core::platform::HttpRequest request;
    request.method = cauth::core::platform::HttpMethod::Get;
    request.url = std::string{kSteamStoreBaseUrl} + std::string{store_path};
    request.headers = {
        {"Cookie", store_cookie_header},
        {"Origin", std::string{kSteamStoreBaseUrl}},
        {"Referer", std::string{kSteamStoreBaseUrl} + "/"},
        {"User-Agent", std::string{kDefaultBrowserUserAgent}},
    };

    const auto response = requester.request(request);
    if (!response.ok) {
        return std::nullopt;
    }

    const auto body = cauth::core::platform::http_body_as_string(response);
    if (!body.has_value()) {
        return std::nullopt;
    }

    StorePageState state;
    state.cookies = collect_response_cookies(response, "store.steampowered.com");
    const auto session_id = find_json_string_value(*body, "g_sessionID");
    if (!session_id.empty()) {
        state.session_id = std::string{session_id};
    } else {
        const auto marker = body->find("var g_sessionID = \"");
        if (marker != std::string::npos) {
            const auto begin = marker + std::string_view{"var g_sessionID = \""}.size();
            const auto end = body->find('"', begin);
            if (end != std::string::npos && end > begin) {
                state.session_id = body->substr(begin, end - begin);
            }
        }
    }
    const auto webapi_token_marker = body->find("window.g_wapit=\"");
    if (webapi_token_marker != std::string::npos) {
        const auto begin = webapi_token_marker + std::string_view{"window.g_wapit=\""}.size();
        const auto end = body->find('"', begin);
        if (end != std::string::npos && end > begin) {
            state.webapi_token = body->substr(begin, end - begin);
        }
    }

    return state;
}

std::optional<AjaxRefreshResponse> perform_ajax_refresh(
    SteamHttpRequester& requester,
    const std::vector<std::string>& cookies,
    std::string_view store_path) {
    const auto login_cookie_header =
        build_cookie_request_header(cookies, "login.steampowered.com");
    if (login_cookie_header.empty()) {
        return std::nullopt;
    }

    const auto absolute_redir = std::string{kSteamStoreBaseUrl} + std::string{store_path};
    const auto response = requester.request(
        make_form_request(
            kAjaxRefreshUrl,
            {{"redir", absolute_redir}},
            {
                {"Cookie", login_cookie_header},
                {"Origin", std::string{kSteamStoreBaseUrl}},
                {"Referer", absolute_redir},
                {"User-Agent", std::string{kDefaultBrowserUserAgent}},
            }));
    if (!response.ok) {
        return std::nullopt;
    }

    const auto body = cauth::core::platform::http_body_as_string(response);
    if (!body.has_value()) {
        return std::nullopt;
    }
    if (!find_json_bool(*body, "success").value_or(false)) {
        return std::nullopt;
    }

    const auto login_url = find_json_string(*body, "login_url");
    const auto steam_id = find_json_string(*body, "steamID");
    const auto nonce = find_json_string(*body, "nonce");
    const auto redir = find_json_string(*body, "redir");
    const auto auth = find_json_string(*body, "auth");
    if (!login_url.has_value() || !steam_id.has_value() || !nonce.has_value() ||
        !redir.has_value() || !auth.has_value()) {
        return std::nullopt;
    }

    AjaxRefreshResponse result;
    result.login_url = *login_url;
    result.fields.emplace_back("steamID", *steam_id);
    result.fields.emplace_back("nonce", *nonce);
    result.fields.emplace_back("redir", *redir);
    result.fields.emplace_back("auth", *auth);
    return result;
}

std::optional<SetTokenResponse> perform_settoken(
    SteamHttpRequester& requester,
    std::vector<std::string>& cookies,
    const AjaxRefreshResponse& refresh,
    std::string_view session_id,
    std::string_view prior_token,
    std::string_view referer_url) {
    const auto store_cookie_header =
        build_cookie_request_header(cookies, "store.steampowered.com");
    if (store_cookie_header.empty()) {
        return std::nullopt;
    }

    auto fields = refresh.fields;
    if (!session_id.empty()) {
        fields.emplace_back("sessionid", std::string{session_id});
    }
    if (!prior_token.empty()) {
        fields.emplace_back("prior", std::string{prior_token});
    }

    const auto response = requester.request(
        make_form_request(
            refresh.login_url,
            fields,
            {
                {"Cookie", store_cookie_header},
                {"Origin", std::string{kSteamStoreBaseUrl}},
                {"Referer", std::string{referer_url}},
                {"User-Agent", std::string{kDefaultBrowserUserAgent}},
            }));
    if (!response.ok) {
        return std::nullopt;
    }

    merge_cookie_vectors(cookies, collect_response_cookies(response, "store.steampowered.com"));

    const auto body = cauth::core::platform::http_body_as_string(response);
    if (!body.has_value()) {
        return std::nullopt;
    }

    const auto token = find_json_string(*body, "token");
    if (!token.has_value()) {
        return std::nullopt;
    }
    return SetTokenResponse{*token};
}

} // namespace

cauth::core::platform::HttpResponse
PlatformSteamHttpRequester::request(const cauth::core::platform::HttpRequest& request) {
    return cauth::core::platform::perform_platform_http_request(request);
}

std::string build_cookie_request_header(const std::vector<std::string>& cookies,
                                        std::string_view preferred_domain) {
    struct CookieEntry {
        std::string name;
        std::string value;
        std::string domain;
    };

    auto extract_domain = [](std::string_view cookie) {
        constexpr std::string_view kDomain = "Domain=";
        constexpr std::string_view kDomainLower = "domain=";
        auto domain_pos = cookie.find(kDomain);
        std::size_t value_start = 0;
        if (domain_pos != std::string_view::npos) {
            value_start = domain_pos + kDomain.size();
        } else {
            domain_pos = cookie.find(kDomainLower);
            if (domain_pos == std::string_view::npos) {
                return std::string{};
            }
            value_start = domain_pos + kDomainLower.size();
        }

        const auto value_end = cookie.find(';', value_start);
        return trim_ascii(cookie.substr(
            value_start,
            value_end == std::string_view::npos ? std::string_view::npos : value_end - value_start));
    };

    std::map<std::string, CookieEntry, std::less<>> cookie_values;
    for (const auto& cookie : cookies) {
        const auto pair_end = cookie.find(';');
        const auto pair = trim_ascii(cookie.substr(0, pair_end));
        const auto equals = pair.find('=');
        if (equals == std::string::npos || equals == 0) {
            continue;
        }

        CookieEntry entry;
        entry.name = trim_ascii(pair.substr(0, equals));
        entry.value = trim_ascii(pair.substr(equals + 1));
        entry.domain = extract_domain(cookie);
        if (entry.name.empty()) {
            continue;
        }

        if (!preferred_domain.empty()) {
            const auto matches_domain = ascii_iequals(entry.domain, preferred_domain);
            if (!matches_domain) {
                continue;
            }
        }

        const auto existing = cookie_values.find(entry.name);
        if (existing == cookie_values.end()) {
            cookie_values[entry.name] = std::move(entry);
            continue;
        }

        if (!preferred_domain.empty() &&
            !ascii_iequals(existing->second.domain, preferred_domain) &&
            ascii_iequals(entry.domain, preferred_domain)) {
            existing->second = std::move(entry);
            continue;
        }
    }

    std::ostringstream header;
    bool first = true;
    for (const auto& [name, entry] : cookie_values) {
        if (!first) {
            header << "; ";
        }
        first = false;
        header << name << '=' << entry.value;
    }
    return header.str();
}

std::optional<std::string> extract_steam_refresh_token_from_web_cookies(
    const std::vector<std::string>& cookies,
    std::uint64_t expected_steam_id) {
    auto percent_decode = [](std::string_view encoded) {
        std::string decoded;
        decoded.reserve(encoded.size());
        for (std::size_t index = 0; index < encoded.size(); ++index) {
            const auto ch = encoded[index];
            if (ch == '%' && index + 2 < encoded.size()) {
                unsigned int byte = 0;
                const auto parsed = std::from_chars(
                    encoded.data() + index + 1, encoded.data() + index + 3, byte, 16);
                if (parsed.ec == std::errc{}) {
                    decoded.push_back(static_cast<char>(byte));
                    index += 2;
                    continue;
                }
            }
            if (ch == '+') {
                decoded.push_back(' ');
                continue;
            }
            decoded.push_back(ch);
        }
        return decoded;
    };

    for (const auto& cookie : cookies) {
        if (cookie.rfind("steamRefresh_steam=", 0) != 0) {
            continue;
        }

        const auto pair_end = cookie.find(';');
        const auto encoded_value =
            cookie.substr(sizeof("steamRefresh_steam=") - 1,
                          pair_end == std::string::npos ? std::string::npos
                                                        : pair_end - (sizeof("steamRefresh_steam=") - 1));
        const auto decoded = percent_decode(encoded_value);
        const auto separator = decoded.find("||");
        if (separator == std::string::npos) {
            if (!decoded.empty()) {
                return decoded;
            }
            continue;
        }

        if (expected_steam_id != 0) {
            std::uint64_t cookie_steam_id = 0;
            const auto parsed = std::from_chars(
                decoded.data(), decoded.data() + separator, cookie_steam_id);
            if (parsed.ec == std::errc{} && cookie_steam_id != expected_steam_id) {
                continue;
            }
        }

        const auto token = decoded.substr(separator + 2);
        if (!token.empty()) {
            return token;
        }
    }

    return std::nullopt;
}

SteamWebCookieService::SteamWebCookieService(SteamHttpRequester& requester) : requester_(&requester) {}

SteamWebCookieResult
SteamWebCookieService::get_web_cookies(const cauth::core::session::AuthSession& session,
                                       std::string_view store_path) const {
    if (session.refresh_token.empty()) {
        return {false, "A refresh token is required to finalize a web login", {}};
    }
    if (steam_id(session) == 0) {
        return {false, "A SteamID is required to finalize a web login", {}};
    }

    const auto session_id = random_session_id();
    auto finalize_request = make_multipart_request(
        kFinalizeLoginUrl,
        {
            {"nonce", session.refresh_token},
            {"sessionid", session_id},
            {"redir", std::string{kFinalizeLoginRedirect}},
        },
        {
            {"Origin", std::string{kSteamCommunityOrigin}},
            {"Referer", std::string{kSteamCommunityOrigin} + "/"},
        });

    const auto finalize_response = requester_->request(finalize_request);
    if (!finalize_response.ok) {
        return {false, "finalizelogin failed: " + finalize_response.error_message, {}};
    }

    const auto finalize_body = cauth::core::platform::http_body_as_string(finalize_response);
    if (!finalize_body.has_value()) {
        return {false, "finalizelogin body decode failed", {}};
    }

    const auto parsed = parse_finalize_login_response(*finalize_body);
    if (!parsed.has_value()) {
        return {
            false,
            "finalizelogin response parse failed: " +
                trim_ascii(finalize_body->substr(0, std::min<std::size_t>(finalize_body->size(), 240))),
            {},
        };
    }

    auto cookies = collect_response_cookies(finalize_response, host_from_url(kFinalizeLoginUrl));
    for (const auto& transfer : parsed->transfers) {
        auto fields = transfer.params;
        fields.emplace_back("steamID", std::to_string(steam_id(session)));
        const auto transfer_response = requester_->request(make_multipart_request(transfer.url, fields));
        if (!transfer_response.ok) {
            return {false, "login transfer failed: " + transfer_response.error_message, {}};
        }

        const auto transfer_cookies =
            collect_response_cookies(transfer_response, host_from_url(transfer.url));
        if (transfer_cookies.empty()) {
            return {false, "login transfer did not return cookies", {}};
        }
        cookies.insert(cookies.end(), transfer_cookies.begin(), transfer_cookies.end());
    }

    cookies.erase(
        std::remove_if(cookies.begin(), cookies.end(),
                       [](const std::string& cookie) {
                           return cookie.rfind("sessionid=", 0) == 0;
                       }),
        cookies.end());

    std::set<std::string> cookie_domains;
    for (const auto& cookie : cookies) {
        const auto domain_pos = cookie.find("Domain=");
        if (domain_pos == std::string::npos) {
            continue;
        }
        const auto value_start = domain_pos + 7;
        const auto value_end = cookie.find(';', value_start);
        const auto domain = cookie.substr(
            value_start, value_end == std::string::npos ? std::string::npos
                                                        : value_end - value_start);
        if (!ascii_iequals(domain, "login.steampowered.com")) {
            cookie_domains.insert(domain);
        }
    }

    for (const auto& domain : cookie_domains) {
        cookies.push_back("sessionid=" + session_id +
                          "; Path=/; Secure; SameSite=None; Domain=" + domain);
    }

    SteamWebCookieResult result;
    result.ok = true;
    result.cookies = std::move(cookies);
    result.login_cookie_header =
        build_cookie_request_header(result.cookies, "login.steampowered.com");
    result.community_cookie_header =
        build_cookie_request_header(result.cookies, "steamcommunity.com");
    result.store_cookie_header =
        build_cookie_request_header(result.cookies, "store.steampowered.com");

    if (store_path.empty()) {
        store_path = "/";
    }

    if (!result.store_cookie_header.empty()) {
        auto store_page_state = fetch_store_page_state(*requester_, result.cookies, store_path);
        if (store_page_state.has_value()) {
            merge_cookie_vectors(result.cookies, store_page_state->cookies);
            result.store_session_id = store_page_state->session_id;
            result.store_webapi_token = store_page_state->webapi_token;
            result.store_cookie_header =
                build_cookie_request_header(result.cookies, "store.steampowered.com");
        }

        const auto refresh = perform_ajax_refresh(*requester_, result.cookies, store_path);
        if (refresh.has_value()) {
            const auto referer_url = std::string{kSteamStoreBaseUrl} + std::string{store_path};
            const auto settoken = perform_settoken(
                *requester_,
                result.cookies,
                *refresh,
                result.store_session_id,
                result.store_webapi_token,
                referer_url);
            if (settoken.has_value() && !settoken->token.empty()) {
                result.store_webapi_token = settoken->token;
                result.store_cookie_header =
                    build_cookie_request_header(result.cookies, "store.steampowered.com");
            }
        }
    }

    result.login_cookie_header =
        build_cookie_request_header(result.cookies, "login.steampowered.com");
    result.community_cookie_header =
        build_cookie_request_header(result.cookies, "steamcommunity.com");
    result.store_cookie_header =
        build_cookie_request_header(result.cookies, "store.steampowered.com");
    return result;
}

} // namespace cauth::steam::auth
