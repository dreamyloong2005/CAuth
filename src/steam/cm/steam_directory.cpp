#include "steam/cm/steam_directory.hpp"

#include "core/platform/http_client.hpp"

#include <charconv>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace cauth::core::cm {
namespace {

constexpr std::string_view kSteamApiBaseUrl = "https://api.steampowered.com";

std::string url_encode(std::string_view value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;

    for (const auto ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' ||
            byte == '~') {
            encoded << static_cast<char>(byte);
            continue;
        }

        encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }

    return encoded.str();
}

std::optional<std::uint16_t> parse_port(std::string_view value) {
    unsigned parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed > 65535U) {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(parsed);
}

std::optional<CmServerEndpoint> parse_tcp_endpoint(std::string_view value) {
    const auto separator = value.rfind(':');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 >= value.size()) {
        return std::nullopt;
    }

    const auto port = parse_port(value.substr(separator + 1));
    if (!port.has_value()) {
        return std::nullopt;
    }

    return CmServerEndpoint{std::string{value.substr(0, separator)}, *port,
                            CmServerProtocol::Tcp};
}

std::optional<CmServerEndpoint> parse_websocket_endpoint(std::string_view value) {
    constexpr std::string_view wss = "wss://";
    constexpr std::string_view ws = "ws://";

    if (value.rfind(wss, 0) != 0 && value.rfind(ws, 0) != 0) {
        auto endpoint = parse_tcp_endpoint(value);
        if (!endpoint.has_value()) {
            return std::nullopt;
        }
        endpoint->protocol = CmServerProtocol::WebSocket;
        return endpoint;
    }

    const auto scheme_end = value.find("://");
    auto authority_start = scheme_end + 3;
    auto authority_end = value.find('/', authority_start);
    if (authority_end == std::string_view::npos) {
        authority_end = value.size();
    }

    const auto authority = value.substr(authority_start, authority_end - authority_start);
    const auto separator = authority.rfind(':');
    std::uint16_t port = value.rfind(wss, 0) == 0 ? 443 : 80;
    std::string address;

    if (separator != std::string_view::npos) {
        const auto parsed_port = parse_port(authority.substr(separator + 1));
        if (!parsed_port.has_value()) {
            return std::nullopt;
        }
        port = *parsed_port;
        address = std::string{authority.substr(0, separator)};
    } else {
        address = std::string{authority};
    }

    if (address.empty()) {
        return std::nullopt;
    }

    return CmServerEndpoint{address, port, CmServerProtocol::WebSocket};
}

std::vector<std::string_view> parse_string_array(std::string_view json, std::string_view key) {
    std::vector<std::string_view> values;
    const auto needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) {
        return values;
    }

    position = json.find('[', position + needle.size());
    if (position == std::string_view::npos) {
        return values;
    }

    const auto end = json.find(']', position + 1);
    if (end == std::string_view::npos) {
        return values;
    }

    auto cursor = position + 1;
    while (cursor < end) {
        cursor = json.find('"', cursor);
        if (cursor == std::string_view::npos || cursor >= end) {
            break;
        }

        const auto start = cursor + 1;
        cursor = start;
        bool escaped = false;
        while (cursor < end) {
            const auto ch = json[cursor];
            if (!escaped && ch == '"') {
                values.push_back(json.substr(start, cursor - start));
                ++cursor;
                break;
            }

            escaped = !escaped && ch == '\\';
            if (ch != '\\') {
                escaped = false;
            }
            ++cursor;
        }
    }

    return values;
}

std::vector<std::string_view> find_all_json_strings(std::string_view json, std::string_view key) {
    std::vector<std::string_view> values;
    const auto needle = "\"" + std::string{key} + "\"";
    std::size_t search_from = 0;

    while (true) {
        auto position = json.find(needle, search_from);
        if (position == std::string_view::npos) {
            break;
        }

        position = json.find(':', position + needle.size());
        if (position == std::string_view::npos) {
            break;
        }

        position = json.find('"', position + 1);
        if (position == std::string_view::npos) {
            break;
        }

        const auto start = position + 1;
        auto end = start;
        bool escaped = false;
        while (end < json.size()) {
            const auto ch = json[end];
            if (!escaped && ch == '"') {
                values.push_back(json.substr(start, end - start));
                search_from = end + 1;
                break;
            }

            escaped = !escaped && ch == '\\';
            if (ch != '\\') {
                escaped = false;
            }
            ++end;
        }

        if (end >= json.size()) {
            break;
        }
    }

    return values;
}

} // namespace

std::vector<CmServerEndpoint> parse_cm_server_list_response(std::string_view json,
                                                            CmServerProtocol protocol) {
    std::vector<CmServerEndpoint> servers;
    const auto key = protocol == CmServerProtocol::WebSocket ? "serverlist_websockets"
                                                             : "serverlist";
    auto entries = parse_string_array(json, key);
    if (entries.empty()) {
        entries = protocol == CmServerProtocol::WebSocket
                      ? find_all_json_strings(json, "endpoint")
                      : find_all_json_strings(json, "legacy_endpoint");
    }
    if (entries.empty() && protocol == CmServerProtocol::Tcp) {
        entries = find_all_json_strings(json, "endpoint");
    }

    for (const auto entry : entries) {
        auto endpoint = protocol == CmServerProtocol::WebSocket
                            ? parse_websocket_endpoint(entry)
                            : parse_tcp_endpoint(entry);
        if (endpoint.has_value()) {
            servers.push_back(*endpoint);
        }
    }

    return servers;
}

CmServerListResult SteamDirectoryClient::get_cm_servers(const CmServerQuery& query) const {
    const auto cm_type = query.protocol == CmServerProtocol::WebSocket ? "websockets" : "tcp";
    std::ostringstream path;
    path << "/ISteamDirectory/GetCMListForConnect/v1/?cellid=" << query.cell_id
         << "&maxcount=" << query.max_count << "&cmtype=" << url_encode(cm_type);

    platform::HttpRequest request;
    request.url = std::string{kSteamApiBaseUrl} + path.str();
    const auto response = platform::perform_platform_http_request(request);
    if (!response.ok) {
        return {false, response.error_message, {}};
    }

    const auto body = platform::http_body_as_string(response);
    if (!body.has_value()) {
        return {false, "failed to decode Steam Directory response body", {}};
    }

    auto servers = parse_cm_server_list_response(*body, query.protocol);
    if (servers.empty()) {
        return {false, "Steam Directory returned no CM servers", {}};
    }

    return {true, "", std::move(servers)};
}

} // namespace cauth::core::cm
