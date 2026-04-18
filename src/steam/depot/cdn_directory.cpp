#include "steam/depot/cdn_directory.hpp"

#include "core/platform/http_client.hpp"

#include <sstream>
#include <string>

namespace cauth::core::depot {
namespace {

constexpr std::string_view kSteamApiBaseUrl = "https://api.steampowered.com";
constexpr std::int32_t kDirectoryConnectTimeoutMs = 15000;
constexpr std::int32_t kDirectoryReadTimeoutMs = 30000;
constexpr int kDirectoryRequestAttempts = 3;

bool https_is_available(std::string_view value) {
    if (value.empty()) {
        return true;
    }
    return value != "none" && value != "disabled" && value != "unsupported" &&
           value != "unavailable" && value != "false" && value != "0";
}

std::string json_unescape(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    bool escaped = false;
    for (const auto ch : value) {
        if (escaped) {
            switch (ch) {
            case '"':
            case '\\':
            case '/':
                out.push_back(ch);
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            default:
                out.push_back(ch);
                break;
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

std::string find_json_string(std::string_view json, std::string_view key) {
    const auto needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) {
        return {};
    }
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) {
        return {};
    }
    position = json.find('"', position + 1);
    if (position == std::string_view::npos) {
        return {};
    }

    const auto start = position + 1;
    auto end = start;
    bool escaped = false;
    while (end < json.size()) {
        const auto ch = json[end];
        if (!escaped && ch == '"') {
            return json_unescape(json.substr(start, end - start));
        }
        escaped = !escaped && ch == '\\';
        if (ch != '\\') {
            escaped = false;
        }
        ++end;
    }
    return {};
}

bool find_json_bool(std::string_view json, std::string_view key) {
    const auto needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) {
        return false;
    }
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) {
        return false;
    }
    ++position;
    while (position < json.size() && (json[position] == ' ' || json[position] == '\n' ||
                                      json[position] == '\r' || json[position] == '\t')) {
        ++position;
    }
    return json.substr(position, 4) == "true";
}

std::vector<std::string_view> find_server_objects(std::string_view json) {
    std::vector<std::string_view> objects;
    const auto servers_key = json.find("\"servers\"");
    if (servers_key == std::string_view::npos) {
        return objects;
    }
    auto array_start = json.find('[', servers_key);
    if (array_start == std::string_view::npos) {
        return objects;
    }

    std::size_t position = array_start + 1;
    while (position < json.size()) {
        auto object_start = json.find('{', position);
        if (object_start == std::string_view::npos) {
            break;
        }

        int depth = 0;
        bool in_string = false;
        bool escaped = false;
        for (auto cursor = object_start; cursor < json.size(); ++cursor) {
            const auto ch = json[cursor];
            if (in_string) {
                escaped = !escaped && ch == '\\';
                if (!escaped && ch == '"') {
                    in_string = false;
                }
                if (ch != '\\') {
                    escaped = false;
                }
                continue;
            }
            if (ch == '"') {
                in_string = true;
                continue;
            }
            if (ch == '{') {
                ++depth;
                continue;
            }
            if (ch == '}') {
                --depth;
                if (depth == 0) {
                    objects.push_back(json.substr(object_start, cursor - object_start + 1));
                    position = cursor + 1;
                    break;
                }
            }
        }

        if (position <= object_start) {
            break;
        }
    }
    return objects;
}

} // namespace

std::vector<CdnServer> parse_cdn_server_list_response(std::string_view json) {
    std::vector<CdnServer> servers;
    for (const auto object : find_server_objects(json)) {
        CdnServer server;
        server.host = find_json_string(object, "host");
        server.vhost = find_json_string(object, "vhost");
        server.type = find_json_string(object, "type");
        server.proxy_request_path_template = find_json_string(object, "proxy_request_path_template");
        server.use_as_proxy = find_json_bool(object, "use_as_proxy");

        const auto https_support = find_json_string(object, "https_support");
        server.protocol = https_is_available(https_support) ? CdnServerProtocol::Https
                                                            : CdnServerProtocol::Http;
        server.port = server.protocol == CdnServerProtocol::Https ? 443 : 80;

        if (server.vhost.empty()) {
            server.vhost = server.host;
        }
        if (!server.vhost.empty()) {
            servers.push_back(std::move(server));
        }
    }
    return servers;
}

CdnServerListResult CdnDirectoryClient::get_servers_for_steampipe(
    const CdnServerQuery& query) const {
    std::ostringstream path;
    path << "/IContentServerDirectoryService/GetServersForSteamPipe/v1/?cell_id=" << query.cell_id
         << "&max_servers=" << query.max_servers;

    std::string last_error;
    for (int attempt = 1; attempt <= kDirectoryRequestAttempts; ++attempt) {
        platform::HttpRequest request;
        request.url = std::string{kSteamApiBaseUrl} + path.str();
        request.connect_timeout_ms = kDirectoryConnectTimeoutMs;
        request.read_timeout_ms = kDirectoryReadTimeoutMs;

        const auto response = platform::perform_platform_http_request(request);
        if (!response.ok) {
            last_error = response.error_message;
            continue;
        }

        const auto body = platform::http_body_as_string(response);
        if (!body.has_value()) {
            last_error = "failed to decode content server directory response body";
            continue;
        }

        auto servers = parse_cdn_server_list_response(*body);
        if (servers.empty()) {
            last_error = "Content server directory returned no CDN servers";
            continue;
        }

        return {true, "", std::move(servers)};
    }

    if (last_error.empty()) {
        last_error = "Content server directory lookup failed";
    }
    return {false, last_error, {}};
}

} // namespace cauth::core::depot
