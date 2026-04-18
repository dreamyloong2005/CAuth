#include "steam/cloud/steam_cloud_service.hpp"

#include "core/platform/http_client.hpp"
#include "core/session/auth_session.hpp"
#include "steam/auth/steam_session_identity.hpp"
#include "steam/auth/steam_web_cookie_service.hpp"
#include "steam/cloud/steam_cloud_cm_service.hpp"
#include "steam/auth/steam_web_api_auth_transport.hpp"

#include <charconv>
#include <cctype>
#include <cmath>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::steam::cloud {
namespace {

constexpr std::string_view kSteamApiBaseUrl = "https://api.steampowered.com";
constexpr std::string_view kSteamCommunityBaseUrl = "https://steamcommunity.com";
constexpr std::string_view kSteamStoreBaseUrl = "https://store.steampowered.com";
constexpr std::string_view kDefaultBrowserUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

std::size_t skip_json_string(std::string_view json, std::size_t offset) {
    for (std::size_t index = offset + 1; index < json.size(); ++index) {
        if (json[index] == '\\') {
            ++index;
            continue;
        }
        if (json[index] == '"') {
            return index;
        }
    }
    return std::string_view::npos;
}

std::size_t find_matching_brace(std::string_view json, std::size_t start,
                                char open_char, char close_char) {
    std::size_t depth = 0;
    for (std::size_t index = start; index < json.size(); ++index) {
        if (json[index] == '"') {
            index = skip_json_string(json, index);
            if (index == std::string_view::npos) {
                return std::string_view::npos;
            }
            continue;
        }
        if (json[index] == open_char) {
            ++depth;
            continue;
        }
        if (json[index] == close_char) {
            if (depth == 0) {
                return std::string_view::npos;
            }
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    return std::string_view::npos;
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

std::string trim_ascii(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string{value.substr(begin, end - begin)};
}

std::string html_unescape(std::string_view value) {
    std::string result;
    result.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '&') {
            result.push_back(value[index]);
            continue;
        }

        if (value.substr(index, 5) == "&amp;") {
            result.push_back('&');
            index += 4;
            continue;
        }
        if (value.substr(index, 6) == "&quot;") {
            result.push_back('"');
            index += 5;
            continue;
        }
        if (value.substr(index, 4) == "&lt;") {
            result.push_back('<');
            index += 3;
            continue;
        }
        if (value.substr(index, 4) == "&gt;") {
            result.push_back('>');
            index += 3;
            continue;
        }
        if (value.substr(index, 5) == "&#39;") {
            result.push_back('\'');
            index += 4;
            continue;
        }

        result.push_back(value[index]);
    }

    return result;
}

std::string strip_html_tags(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool inside_tag = false;
    for (const auto ch : value) {
        if (ch == '<') {
            inside_tag = true;
            continue;
        }
        if (ch == '>') {
            inside_tag = false;
            continue;
        }
        if (!inside_tag) {
            result.push_back(ch);
        }
    }
    return trim_ascii(html_unescape(result));
}

std::optional<std::string> find_json_string(std::string_view json, std::string_view key) {
    const auto needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    position = json.find('"', position + 1);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    const auto end = skip_json_string(json, position);
    if (end == std::string_view::npos || end <= position + 1) {
        return std::nullopt;
    }
    return json_unescape(json.substr(position + 1, end - position - 1));
}

template <typename T>
std::optional<T> find_json_unsigned(std::string_view json, std::string_view key) {
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
    if (position >= json.size()) {
        return std::nullopt;
    }

    if (json[position] == '"') {
        ++position;
    }

    const auto start = position;
    while (position < json.size() &&
           std::isdigit(static_cast<unsigned char>(json[position])) != 0) {
        ++position;
    }
    if (position == start) {
        return std::nullopt;
    }

    T value = 0;
    const auto parsed = std::from_chars(json.data() + start, json.data() + position, value);
    if (parsed.ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> find_html_attribute(std::string_view html,
                                               std::string_view tag,
                                               std::string_view attribute) {
    const auto tag_position = html.find(tag);
    if (tag_position == std::string_view::npos) {
        return std::nullopt;
    }

    const auto attribute_position = html.find(attribute, tag_position);
    if (attribute_position == std::string_view::npos) {
        return std::nullopt;
    }

    const auto quote_position = html.find('"', attribute_position + attribute.size());
    if (quote_position == std::string_view::npos) {
        return std::nullopt;
    }

    const auto end_quote = html.find('"', quote_position + 1);
    if (end_quote == std::string_view::npos || end_quote <= quote_position + 1) {
        return std::nullopt;
    }

    return html_unescape(html.substr(quote_position + 1, end_quote - quote_position - 1));
}

std::string_view host_from_url(std::string_view url) {
    const auto scheme = url.find("://");
    const auto host_start = scheme == std::string_view::npos ? 0 : scheme + 3;
    const auto host_end = url.find('/', host_start);
    if (host_start >= url.size()) {
        return {};
    }
    if (host_end == std::string_view::npos) {
        return url.substr(host_start);
    }
    return url.substr(host_start, host_end - host_start);
}

bool is_web_store_download_url(std::string_view url) {
    const auto host = host_from_url(url);
    return host == "cdn.steamusercontent.com" &&
           url.find("/filedownload/") != std::string_view::npos;
}

std::uint32_t parse_human_size_bytes(std::string_view value) {
    const auto normalized = trim_ascii(value);
    if (normalized.empty()) {
        return 0;
    }

    std::size_t split = 0;
    while (split < normalized.size() &&
           (std::isdigit(static_cast<unsigned char>(normalized[split])) != 0 ||
            normalized[split] == '.')) {
        ++split;
    }
    if (split == 0) {
        return 0;
    }

    double amount = 0.0;
    try {
        amount = std::stod(normalized.substr(0, split));
    } catch (...) {
        return 0;
    }

    const auto unit = trim_ascii(normalized.substr(split));
    double multiplier = 1.0;
    if (unit == "B" || unit.empty()) {
        multiplier = 1.0;
    } else if (unit == "KB") {
        multiplier = 1024.0;
    } else if (unit == "MB") {
        multiplier = 1024.0 * 1024.0;
    } else if (unit == "GB") {
        multiplier = 1024.0 * 1024.0 * 1024.0;
    }

    const auto bytes = amount * multiplier;
    if (bytes <= 0.0) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::llround(bytes));
}

std::vector<std::string_view> find_html_cells(std::string_view row_html) {
    std::vector<std::string_view> cells;
    std::size_t cursor = 0;
    while (cursor < row_html.size()) {
        const auto td_start = row_html.find("<td", cursor);
        if (td_start == std::string_view::npos) {
            break;
        }
        const auto content_start = row_html.find('>', td_start);
        if (content_start == std::string_view::npos) {
            break;
        }
        const auto td_end = row_html.find("</td>", content_start + 1);
        if (td_end == std::string_view::npos) {
            break;
        }
        cells.push_back(row_html.substr(content_start + 1, td_end - content_start - 1));
        cursor = td_end + 5;
    }
    return cells;
}

std::vector<std::string_view> find_html_table_rows(std::string_view html) {
    std::vector<std::string_view> rows;
    const auto table_start = html.find("<table");
    if (table_start == std::string_view::npos) {
        return rows;
    }
    const auto table_end = html.find("</table>", table_start);
    if (table_end == std::string_view::npos) {
        return rows;
    }

    std::size_t cursor = table_start;
    while (cursor < table_end) {
        const auto row_start = html.find("<tr", cursor);
        if (row_start == std::string_view::npos || row_start >= table_end) {
            break;
        }
        const auto row_content_start = html.find('>', row_start);
        if (row_content_start == std::string_view::npos) {
            break;
        }
        const auto row_end = html.find("</tr>", row_content_start + 1);
        if (row_end == std::string_view::npos || row_end > table_end) {
            break;
        }
        rows.push_back(html.substr(row_content_start + 1, row_end - row_content_start - 1));
        cursor = row_end + 5;
    }
    return rows;
}

std::vector<std::string_view> find_object_array(std::string_view json, std::string_view key) {
    std::vector<std::string_view> objects;
    const auto key_position = json.find("\"" + std::string{key} + "\"");
    if (key_position == std::string_view::npos) {
        return objects;
    }
    const auto array_start = json.find('[', key_position);
    if (array_start == std::string_view::npos) {
        return objects;
    }
    const auto array_end = find_matching_brace(json, array_start, '[', ']');
    if (array_end == std::string_view::npos) {
        return objects;
    }

    std::size_t cursor = array_start + 1;
    while (cursor < array_end) {
        const auto object_start = json.find('{', cursor);
        if (object_start == std::string_view::npos || object_start >= array_end) {
            break;
        }
        const auto object_end = find_matching_brace(json, object_start, '{', '}');
        if (object_end == std::string_view::npos || object_end > array_end) {
            break;
        }
        objects.push_back(json.substr(object_start, object_end - object_start + 1));
        cursor = object_end + 1;
    }

    return objects;
}

std::optional<std::string> build_store_cookie_header_for_request(const SteamCloudRequest& request) {
    if (!request.store_cookie_header.empty()) {
        return request.store_cookie_header;
    }
    if (request.refresh_token.empty() || request.steam_id == 0) {
        return std::nullopt;
    }

    cauth::core::session::AuthSession session;
    session.provider = std::string{cauth::steam::auth::kSteamAuthProvider};
    session.subject_id = std::to_string(request.steam_id);
    session.refresh_token = request.refresh_token;
    session.access_token = request.access_token;
    session.session_type = request.session_type;

    cauth::steam::auth::PlatformSteamHttpRequester requester;
    cauth::steam::auth::SteamWebCookieService cookie_service{requester};
    const auto cookie_result = cookie_service.get_web_cookies(
        session, "/account/remotestorageapp/?appid=" + std::to_string(request.app_id));
    if (!cookie_result.ok) {
        return std::nullopt;
    }

    if (!cookie_result.store_cookie_header.empty()) {
        return cookie_result.store_cookie_header;
    }
    return cauth::steam::auth::build_cookie_request_header(
        cookie_result.cookies, "store.steampowered.com");
}

SteamCloudFileListResult parse_remote_storage_app_page(std::uint32_t app_id, std::string_view html) {
    SteamCloudFileListResult result;
    result.app_id = app_id;
    result.ok = true;
    result.eresult = 1;

    for (const auto row : find_html_table_rows(html)) {
        const auto cells = find_html_cells(row);
        if (cells.size() < 5) {
            continue;
        }

        SteamCloudFileEntry entry;
        entry.app_id = app_id;

        const auto folder = strip_html_tags(cells[0]);
        auto filename = strip_html_tags(cells[1]);
        if (filename.empty()) {
            continue;
        }
        if (!folder.empty() && filename.find('/') == std::string::npos &&
            filename.find('\\') == std::string::npos) {
            entry.filename = folder + "/" + filename;
        } else {
            entry.filename = std::move(filename);
        }
        entry.file_size = parse_human_size_bytes(strip_html_tags(cells[2]));
        entry.url = find_html_attribute(cells[4], "<a", "href=").value_or("");
        result.files.push_back(std::move(entry));
    }

    result.total_files = static_cast<std::uint32_t>(result.files.size());
    result.message = "ok";
    return result;
}

SteamCloudFileListResult fetch_remote_file_list_via_store_page(const SteamCloudRequest& request) {
    SteamCloudFileListResult result;
    result.app_id = request.app_id;

    if (request.app_id == 0) {
        result.message = "app_id is required";
        return result;
    }
    if (!cauth::core::platform::is_platform_http_client_available()) {
        result.message = "platform HTTP client is not available";
        return result;
    }

    const auto store_cookie_header = build_store_cookie_header_for_request(request);
    if (!store_cookie_header.has_value() || store_cookie_header->empty()) {
        result.message = "web cookie auth materialization failed for store.steampowered.com";
        return result;
    }

    cauth::core::platform::HttpRequest http_request;
    http_request.method = cauth::core::platform::HttpMethod::Get;
    http_request.url = std::string{kSteamStoreBaseUrl} +
                       "/account/remotestorageapp/?appid=" + std::to_string(request.app_id);
    http_request.headers.push_back({"Cookie", *store_cookie_header});
    http_request.headers.push_back({"Origin", std::string{kSteamStoreBaseUrl}});
    http_request.headers.push_back({"Referer", std::string{kSteamStoreBaseUrl} + "/"});

    const auto response = cauth::core::platform::perform_platform_http_request(http_request);
    if (!response.ok) {
        result.message = response.error_message.empty() ? "Steam Cloud store page request failed"
                                                        : response.error_message;
        return result;
    }

    const auto body = cauth::core::platform::http_body_as_string(response);
    if (!body.has_value()) {
        result.message = "failed to decode Steam Cloud store page";
        return result;
    }

    return parse_remote_storage_app_page(request.app_id, *body);
}

std::uint32_t parse_x_eresult_header(const cauth::core::platform::HttpResponse& response) {
    const auto values = cauth::core::platform::http_header_values(response, "x-eresult");
    if (values.empty()) {
        return response.ok ? 1U : 0U;
    }

    std::uint32_t value = 0;
    const auto parsed =
        std::from_chars(values.front().data(), values.front().data() + values.front().size(), value);
    if (parsed.ec != std::errc{}) {
        return 0;
    }
    return value;
}

SteamCloudBackend resolve_sync_backend(const SteamCloudRequest& request) {
    if (request.backend != SteamCloudBackend::Auto) {
        return request.backend;
    }
    if (!request.web_cookie_header.empty() || !request.store_cookie_header.empty()) {
        return SteamCloudBackend::WebApi;
    }
    if (request.session_type == cauth::steam::auth::kSteamSessionTypeSteamClient) {
        return SteamCloudBackend::CmCloud;
    }
    if (request.session_type == cauth::steam::auth::kSteamSessionTypeWebBrowser ||
        request.session_type == cauth::steam::auth::kSteamSessionTypeMobileApp) {
        return SteamCloudBackend::WebApi;
    }
    if (!request.access_token.empty()) {
        return SteamCloudBackend::WebApi;
    }
    return SteamCloudBackend::CmCloud;
}

} // namespace

SteamCloudRequest materialize_cloud_web_api_auth(const SteamCloudRequest& request,
                                                 std::string* error_message) {
    auto effective = request;
    if (!effective.access_token.empty() || !effective.web_cookie_header.empty() ||
        !effective.store_cookie_header.empty()) {
        return effective;
    }
    if (effective.session_type != cauth::steam::auth::kSteamSessionTypeWebBrowser) {
        return effective;
    }
    if (effective.refresh_token.empty() || effective.steam_id == 0) {
        return effective;
    }

    cauth::core::session::AuthSession session;
    session.provider = std::string{cauth::steam::auth::kSteamAuthProvider};
    session.subject_id = std::to_string(effective.steam_id);
    session.refresh_token = effective.refresh_token;
    session.access_token = effective.access_token;
    session.session_type = effective.session_type;

    cauth::steam::auth::PlatformSteamHttpRequester requester;
    cauth::steam::auth::SteamWebCookieService cookie_service{requester};
    const auto cookie_result = cookie_service.get_web_cookies(
        session, "/account/remotestorageapp/?appid=" + std::to_string(effective.app_id));
    if (!cookie_result.ok) {
        if (error_message != nullptr) {
            *error_message = "web cookie auth materialization failed: " + cookie_result.error_message;
        }
        return effective;
    }

    effective.web_cookie_header = !cookie_result.community_cookie_header.empty()
                                      ? cookie_result.community_cookie_header
                                      : cauth::steam::auth::build_cookie_request_header(
                                            cookie_result.cookies, "steamcommunity.com");
    effective.store_cookie_header = !cookie_result.store_cookie_header.empty()
                                        ? cookie_result.store_cookie_header
                                        : cauth::steam::auth::build_cookie_request_header(
                                              cookie_result.cookies, "store.steampowered.com");

    const auto derived_refresh_token =
        cauth::steam::auth::extract_steam_refresh_token_from_web_cookies(
            cookie_result.cookies, effective.steam_id);
    if (!derived_refresh_token.has_value() || derived_refresh_token->empty()) {
        return effective;
    }

    try {
        cauth::steam::auth::SteamWebApiAuthenticationTransport transport;
        const auto generated = transport.generate_access_token_for_app(
            cauth::steam::auth::SteamGenerateAccessTokenForAppRequest{
                effective.steam_id,
                *derived_refresh_token,
                cauth::steam::auth::SteamLoginPlatformType::WebBrowser,
            });
        if (generated.result.ok && generated.value.has_value() &&
            !generated.value->access_token.empty()) {
            effective.access_token = generated.value->access_token;
            effective.web_cookie_header.clear();
            effective.store_cookie_header.clear();
        } else if (error_message != nullptr &&
                   (!generated.result.ok || !generated.value.has_value())) {
            *error_message = "web cloud access token derivation failed: " +
                             generated.result.error_message;
        }
    } catch (const std::exception& ex) {
        if (error_message != nullptr) {
            *error_message =
                "web cloud access token derivation threw: " + std::string{ex.what()};
        }
    } catch (...) {
        if (error_message != nullptr) {
            *error_message = "web cloud access token derivation threw an unknown exception";
        }
    }

    return effective;
}

std::string build_enumerate_user_files_url(const SteamCloudRequest& request,
                                           std::uint32_t count,
                                           std::uint32_t start_index,
                                           bool extended_details) {
    auto url = std::string{request.web_cookie_header.empty() ? kSteamApiBaseUrl
                                                             : kSteamCommunityBaseUrl} +
               "/ICloudService/EnumerateUserFiles/v1/?appid=" + std::to_string(request.app_id) +
               "&extended_details=" + (extended_details ? "1" : "0") +
               "&count=" + std::to_string(count) +
               "&start_index=" + std::to_string(start_index);
    if (!request.access_token.empty()) {
        url.insert(
            url.find('?') + 1,
            "access_token=" + cauth::steam::auth::steam_web_api_url_encode(request.access_token) +
                "&");
    }
    return url;
}

SteamCloudFileListResult parse_enumerate_user_files_response(std::uint32_t app_id,
                                                             std::string_view json,
                                                             std::uint32_t eresult) {
    SteamCloudFileListResult result;
    result.app_id = app_id;
    result.eresult = eresult;
    result.ok = eresult == 1;

    const auto total_files = find_json_unsigned<std::uint32_t>(json, "total_files");
    if (total_files.has_value()) {
        result.total_files = *total_files;
    }

    for (const auto object : find_object_array(json, "files")) {
        SteamCloudFileEntry entry;
        entry.app_id = find_json_unsigned<std::uint32_t>(object, "appid").value_or(app_id);
        entry.ugc_id = find_json_unsigned<std::uint64_t>(object, "ugcid").value_or(0);
        entry.filename = find_json_string(object, "filename").value_or("");
        entry.timestamp = find_json_unsigned<std::uint64_t>(object, "timestamp").value_or(0);
        entry.file_size = find_json_unsigned<std::uint32_t>(object, "file_size").value_or(0);
        entry.url = find_json_string(object, "url").value_or("");
        entry.steam_id_creator =
            find_json_unsigned<std::uint64_t>(object, "steamid_creator").value_or(0);
        entry.flags = find_json_unsigned<std::uint32_t>(object, "flags").value_or(0);
        entry.platforms_to_sync = find_json_string(object, "platforms_to_sync").value_or("");
        entry.file_sha = find_json_string(object, "file_sha").value_or("");
        result.files.push_back(std::move(entry));
    }

    if (!result.ok && result.message.empty()) {
        result.message = "Steam Cloud enumerate failed";
    } else if (result.ok) {
        result.message = "ok";
    }

    return result;
}

SteamCloudFileListResult fetch_remote_file_list_via_web_api(const SteamCloudRequest& request,
                                                            std::uint32_t count,
                                                            std::uint32_t start_index,
                                                            bool extended_details) {
    SteamCloudFileListResult result;
    result.app_id = request.app_id;
    try {
        std::string auth_error;
        auto effective_request = materialize_cloud_web_api_auth(request, &auth_error);
        result.app_id = effective_request.app_id;

        if (effective_request.app_id == 0) {
            result.message = "app_id is required";
            return result;
        }
        if (effective_request.access_token.empty() && effective_request.web_cookie_header.empty() &&
            effective_request.store_cookie_header.empty()) {
            result.message = auth_error.empty()
                                 ? "access token with read_cloud scope or web cookie session is required"
                                 : auth_error;
            return result;
        }
        if (effective_request.access_token.empty() &&
            effective_request.session_type == cauth::steam::auth::kSteamSessionTypeWebBrowser) {
            return fetch_remote_file_list_via_store_page(effective_request);
        }
        if (!cauth::core::platform::is_platform_http_client_available()) {
            result.message = "platform HTTP client is not available";
            return result;
        }

        cauth::core::platform::HttpRequest http_request;
        http_request.method = cauth::core::platform::HttpMethod::Get;
        http_request.url =
            build_enumerate_user_files_url(effective_request, count, start_index, extended_details);
        if (!effective_request.web_cookie_header.empty()) {
            http_request.headers.push_back({"Cookie", effective_request.web_cookie_header});
            http_request.headers.push_back({"Origin", std::string{kSteamCommunityBaseUrl}});
            http_request.headers.push_back({"Referer", std::string{kSteamCommunityBaseUrl} + "/"});
        }

        const auto response = cauth::core::platform::perform_platform_http_request(http_request);
        const auto eresult = parse_x_eresult_header(response);
        result.eresult = eresult;
        if (!response.ok) {
            result.message = response.error_message.empty() ? "Steam Cloud request failed"
                                                            : response.error_message;
            return result;
        }

        const auto body = cauth::core::platform::http_body_as_string(response);
        if (!body.has_value()) {
            result.message = "failed to decode Steam Cloud response body";
            return result;
        }

        result = parse_enumerate_user_files_response(effective_request.app_id, *body, eresult);
        if (!result.ok && result.message.empty()) {
            result.message = "Steam Cloud enumerate failed";
        }
    } catch (const std::exception& ex) {
        result.message = "Steam Cloud web API path threw: " + std::string{ex.what()};
        return result;
    } catch (...) {
        result.message = "Steam Cloud web API path threw an unknown exception";
        return result;
    }
    return result;
}

SteamCloudFileListResult fetch_remote_file_list(const SteamCloudRequest& request,
                                                std::uint32_t count,
                                                std::uint32_t start_index,
                                                bool extended_details) {
    switch (resolve_sync_backend(request)) {
    case SteamCloudBackend::WebApi:
        return fetch_remote_file_list_via_web_api(request, count, start_index, extended_details);
    case SteamCloudBackend::CmCloud:
        return fetch_remote_file_list_via_cm(request, count, start_index, extended_details);
    case SteamCloudBackend::Auto:
    default: {
        SteamCloudFileListResult result;
        result.app_id = request.app_id;
        result.message = "sync backend resolution failed";
        return result;
    }
    }
}

SteamCloudDownloadResult download_remote_file(const SteamCloudRequest& request,
                                              const SteamCloudFileEntry& file) {
    switch (resolve_sync_backend(request)) {
    case SteamCloudBackend::CmCloud:
        return download_remote_file_via_cm(request, file);
    case SteamCloudBackend::WebApi:
        break;
    case SteamCloudBackend::Auto:
    default:
        break;
    }

    SteamCloudDownloadResult result;
    result.file_size = file.file_size;
    result.raw_file_size = file.file_size;
    if (file.url.empty()) {
        result.message = "remote file URL is missing";
        return result;
    }
    if (!cauth::core::platform::is_platform_http_client_available()) {
        result.message = "platform HTTP client is not available";
        return result;
    }

    cauth::core::platform::HttpRequest http_request;
    http_request.method = cauth::core::platform::HttpMethod::Get;
    http_request.url = file.url;
    if (request.session_type == cauth::steam::auth::kSteamSessionTypeWebBrowser &&
        is_web_store_download_url(file.url)) {
        if (const auto store_cookie_header = build_store_cookie_header_for_request(request);
            store_cookie_header.has_value() && !store_cookie_header->empty()) {
            http_request.headers.push_back({"Cookie", *store_cookie_header});
        }
        http_request.headers.push_back({"Origin", std::string{kSteamStoreBaseUrl}});
        http_request.headers.push_back(
            {"Referer",
             std::string{kSteamStoreBaseUrl} +
                 "/account/remotestorageapp/?appid=" + std::to_string(request.app_id)});
        http_request.headers.push_back({"User-Agent", std::string{kDefaultBrowserUserAgent}});
    }

    const auto response = cauth::core::platform::perform_platform_http_request(http_request);
    if (!response.ok) {
        if (request.session_type == cauth::steam::auth::kSteamSessionTypeWebBrowser &&
            is_web_store_download_url(file.url) && response.error_message == "HTTP 404") {
            result.message =
                "Steam returned a store file listing, but the web download URL expired or "
                "is not directly downloadable; use `steam auth login` and the CM backend "
                "for full cloud pull/push";
            return result;
        }

        result.message = response.error_message.empty() ? "Steam Cloud download failed"
                                                        : response.error_message;
        return result;
    }

    result.ok = true;
    result.bytes = response.body;
    result.message = "ok";
    return result;
}

} // namespace cauth::steam::cloud
