#include "steam/auth/steam_web_api_auth_transport.hpp"
#include "core/platform/http_client.hpp"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>

namespace cauth::steam::auth {
namespace {

constexpr std::string_view kSteamApiBaseUrl = "https://api.steampowered.com";
constexpr std::string_view kDefaultWebBrowserUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
constexpr std::string_view kMobileUserAgent = "okhttp/4.9.2";
constexpr std::string_view kMobileCookie =
    "mobileClient=android; mobileClientVersion=777777 3.10.3";

struct PlatformContext {
    std::uint64_t platform_value = 1;
    std::string website_id = "Community";
    std::string device_name;
    std::optional<std::uint64_t> os_type;
    std::optional<std::uint64_t> gaming_device_type;
    std::vector<cauth::core::platform::HttpHeader> headers;
};

PlatformContext describe_platform_context(SteamLoginPlatformType platform_type,
                                         std::string_view requested_device_name) {
    PlatformContext context;
    context.device_name =
        requested_device_name.empty() ? "CAuth" : std::string{requested_device_name};

    switch (platform_type) {
    case SteamLoginPlatformType::SteamClient:
        context.platform_value = 1;
        context.website_id = "Client";
        context.os_type = 16;
        context.gaming_device_type = 1;
        break;
    case SteamLoginPlatformType::WebBrowser:
        context.platform_value = 2;
        context.website_id = "Community";
        context.headers.push_back({"Origin", "https://steamcommunity.com"});
        context.headers.push_back({"Referer", "https://steamcommunity.com/"});
        context.headers.push_back({"User-Agent", std::string{kDefaultWebBrowserUserAgent}});
        break;
    case SteamLoginPlatformType::MobileApp:
        context.platform_value = 3;
        context.website_id = "Mobile";
        context.gaming_device_type = 528;
        context.headers.push_back({"User-Agent", std::string{kMobileUserAgent}});
        context.headers.push_back({"Cookie", std::string{kMobileCookie}});
        break;
    }

    return context;
}

std::optional<std::string_view> find_json_string(std::string_view json, std::string_view key) {
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

    const auto start = position + 1;
    auto end = start;
    bool escaped = false;
    while (end < json.size()) {
        const auto ch = json[end];
        if (!escaped && ch == '"') {
            return json.substr(start, end - start);
        }

        escaped = !escaped && ch == '\\';
        if (ch != '\\') {
            escaped = false;
        }
        ++end;
    }

    return std::nullopt;
}

std::optional<std::string_view> find_json_number_like(std::string_view json, std::string_view key) {
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
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) {
        ++position;
    }

    if (position >= json.size()) {
        return std::nullopt;
    }

    if (json[position] == '"') {
        const auto start = position + 1;
        auto end = start;
        while (end < json.size() && json[end] != '"') {
            ++end;
        }

        if (end >= json.size()) {
            return std::nullopt;
        }

        return json.substr(start, end - start);
    }

    const auto start = position;
    while (position < json.size()) {
        const auto ch = json[position];
        if (!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-')) {
            break;
        }
        ++position;
    }

    if (position == start) {
        return std::nullopt;
    }

    return json.substr(start, position - start);
}

std::optional<std::uint64_t> parse_u64(std::string_view value) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }

    return parsed;
}

bool steam_auth_debug_enabled() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t value_length = 0;
    if (_dupenv_s(&value, &value_length, "CAUTH_DEBUG_STEAM_AUTH") != 0 || value == nullptr) {
        return false;
    }

    std::unique_ptr<char, decltype(&std::free)> value_guard{value, std::free};
    return value_length > 0 && std::string_view{value_guard.get()} == "1";
#else
    const auto* value = std::getenv("CAUTH_DEBUG_STEAM_AUTH");
    return value != nullptr && std::string_view{value} == "1";
#endif
}

std::string bytes_to_hex(std::string_view bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto ch : bytes) {
        out << std::setw(2) << static_cast<int>(static_cast<unsigned char>(ch));
    }
    return out.str();
}

std::optional<double> parse_double(std::string_view value) {
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stod(std::string{value}, &consumed);
        if (consumed != value.size()) {
            return std::nullopt;
        }

        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

bool read_proto_varint(std::string_view bytes, std::size_t& offset, std::uint64_t& value) {
    value = 0;
    int shift = 0;

    while (offset < bytes.size() && shift <= 63) {
        const auto byte = static_cast<std::uint8_t>(bytes[offset++]);
        value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            return true;
        }

        shift += 7;
    }

    return false;
}

bool read_proto_length_delimited(std::string_view bytes, std::size_t& offset,
                                 std::string_view& value) {
    std::uint64_t length = 0;
    if (!read_proto_varint(bytes, offset, length) || bytes.size() - offset < length) {
        return false;
    }

    value = bytes.substr(offset, static_cast<std::size_t>(length));
    offset += static_cast<std::size_t>(length);
    return true;
}

bool skip_proto_field(std::string_view bytes, std::size_t& offset, int wire_type) {
    std::uint64_t ignored_varint = 0;
    std::string_view ignored_bytes;

    switch (wire_type) {
    case 0:
        return read_proto_varint(bytes, offset, ignored_varint);
    case 1:
        if (bytes.size() - offset < 8) {
            return false;
        }
        offset += 8;
        return true;
    case 2:
        return read_proto_length_delimited(bytes, offset, ignored_bytes);
    case 5:
        if (bytes.size() - offset < 4) {
            return false;
        }
        offset += 4;
        return true;
    default:
        return false;
    }
}

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view encoded) {
    auto decode_char = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') {
            return ch - 'A';
        }
        if (ch >= 'a' && ch <= 'z') {
            return ch - 'a' + 26;
        }
        if (ch >= '0' && ch <= '9') {
            return ch - '0' + 52;
        }
        if (ch == '+') {
            return 62;
        }
        if (ch == '-') {
            return 62;
        }
        if (ch == '/') {
            return 63;
        }
        if (ch == '_') {
            return 63;
        }
        return -1;
    };

    if (encoded.empty()) {
        return std::nullopt;
    }

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
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            continue;
        }
        normalized.push_back(ch);
    }
    while (normalized.size() % 4 != 0) {
        normalized.push_back('=');
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve((normalized.size() / 4) * 3);

    for (std::size_t index = 0; index < normalized.size(); index += 4) {
        const auto a = decode_char(normalized[index]);
        const auto b = decode_char(normalized[index + 1]);
        const auto c =
            normalized[index + 2] == '=' ? -1 : decode_char(normalized[index + 2]);
        const auto d =
            normalized[index + 3] == '=' ? -1 : decode_char(normalized[index + 3]);

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

void append_varint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
        value >>= 7;
    }

    out.push_back(static_cast<std::uint8_t>(value));
}

void append_tag(std::vector<std::uint8_t>& out, int field_number, int wire_type) {
    append_varint(out, static_cast<std::uint64_t>((field_number << 3) | wire_type));
}

void append_string_field(std::vector<std::uint8_t>& out, int field_number, std::string_view value) {
    if (value.empty()) {
        return;
    }

    append_tag(out, field_number, 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void append_varint_field(std::vector<std::uint8_t>& out, int field_number, std::uint64_t value) {
    append_tag(out, field_number, 0);
    append_varint(out, value);
}

void append_fixed64_field(std::vector<std::uint8_t>& out, int field_number, std::uint64_t value) {
    append_tag(out, field_number, 1);
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_bool_field(std::vector<std::uint8_t>& out, int field_number, bool value) {
    append_varint_field(out, field_number, value ? 1 : 0);
}

void append_message_field(std::vector<std::uint8_t>& out, int field_number,
                          const std::vector<std::uint8_t>& value) {
    if (value.empty()) {
        return;
    }

    append_tag(out, field_number, 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> encode_device_details(const PlatformContext& context) {
    std::vector<std::uint8_t> out;
    append_string_field(out, 1, context.device_name);
    append_varint_field(out, 2, context.platform_value);
    if (context.os_type.has_value()) {
        append_varint_field(out, 3, *context.os_type);
    }
    if (context.gaming_device_type.has_value()) {
        append_varint_field(out, 4, *context.gaming_device_type);
    }
    return out;
}

std::vector<std::uint8_t> encode_begin_auth_session_request_impl(
    const SteamBeginAuthSessionRequest& request) {
    const auto context =
        describe_platform_context(request.platform_type, request.device_friendly_name);

    std::vector<std::uint8_t> out;
    append_string_field(out, 1, context.device_name);
    append_string_field(out, 2, request.account_name);
    append_string_field(out, 3, request.encrypted_password);
    append_varint_field(out, 4, request.encryption_timestamp);
    append_bool_field(out, 5, request.remember_login);
    append_varint_field(out, 6, context.platform_value);
    append_varint_field(out, 7, request.remember_login ? 1 : 0);
    append_string_field(out, 8, context.website_id);
    append_message_field(out, 9, encode_device_details(context));
    append_string_field(out, 10, request.steam_guard_code);
    append_varint_field(out, 12, 2);
    return out;
}

std::vector<std::uint8_t> encode_poll_auth_session_status_request_impl(
    const SteamPollAuthSessionStatusRequest& request) {
    std::vector<std::uint8_t> out;
    append_varint_field(out, 1, request.client_id);
    if (!request.request_id.empty()) {
        append_tag(out, 2, 2);
        append_varint(out, request.request_id.size());
        out.insert(out.end(), request.request_id.begin(), request.request_id.end());
    }
    return out;
}

std::vector<std::uint8_t> encode_generate_access_token_for_app_request_impl(
    const SteamGenerateAccessTokenForAppRequest& request) {
    std::vector<std::uint8_t> out;
    append_string_field(out, 1, request.refresh_token);
    append_fixed64_field(out, 2, request.steam_id);
    append_varint_field(out, 3, 0);
    return out;
}

SteamGuardConfirmationType map_confirmation_type(std::uint64_t value) {
    switch (value) {
    case 2:
        return SteamGuardConfirmationType::EmailCode;
    case 3:
        return SteamGuardConfirmationType::DeviceCode;
    case 4:
        return SteamGuardConfirmationType::DeviceConfirmation;
    case 5:
        return SteamGuardConfirmationType::EmailConfirmation;
    default:
        return SteamGuardConfirmationType::Unknown;
    }
}

std::optional<SteamAllowedConfirmation> parse_allowed_confirmation_protobuf(
    std::string_view bytes) {
    SteamAllowedConfirmation confirmation;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_proto_varint(bytes, offset, tag)) {
            return std::nullopt;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);

        if (field_number == 1 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_proto_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            confirmation.type = map_confirmation_type(value);
            continue;
        }

        if (field_number == 2 && wire_type == 2) {
            std::string_view message;
            if (!read_proto_length_delimited(bytes, offset, message)) {
                return std::nullopt;
            }
            confirmation.message = std::string{message};
            continue;
        }

        if (!skip_proto_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return confirmation;
}

std::optional<SteamBeginAuthSessionResponse> parse_begin_auth_session_protobuf(
    std::string_view bytes) {
    SteamBeginAuthSessionResponse response;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_proto_varint(bytes, offset, tag)) {
            return std::nullopt;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);

        if (field_number == 1 && wire_type == 0) {
            if (!read_proto_varint(bytes, offset, response.client_id)) {
                return std::nullopt;
            }
            continue;
        }

        if (field_number == 2 && wire_type == 2) {
            std::string_view request_id;
            if (!read_proto_length_delimited(bytes, offset, request_id)) {
                return std::nullopt;
            }
            response.request_id.assign(request_id.begin(), request_id.end());
            continue;
        }

        if (field_number == 3 && wire_type == 5) {
            if (bytes.size() - offset < sizeof(float)) {
                return std::nullopt;
            }

            float interval = 0.0F;
            std::memcpy(&interval, bytes.data() + offset, sizeof(float));
            response.interval_seconds = interval;
            offset += sizeof(float);
            continue;
        }

        if (field_number == 4 && wire_type == 2) {
            std::string_view confirmation_bytes;
            if (!read_proto_length_delimited(bytes, offset, confirmation_bytes)) {
                return std::nullopt;
            }

            const auto confirmation = parse_allowed_confirmation_protobuf(confirmation_bytes);
            if (confirmation.has_value()) {
                response.allowed_confirmations.push_back(*confirmation);
            }
            continue;
        }

        if (field_number == 5 && wire_type == 0) {
            if (!read_proto_varint(bytes, offset, response.steam_id)) {
                return std::nullopt;
            }
            continue;
        }

        if (field_number == 8 && wire_type == 2) {
            std::string_view extended_error;
            if (!read_proto_length_delimited(bytes, offset, extended_error)) {
                return std::nullopt;
            }
            response.extended_error_message = std::string{extended_error};
            continue;
        }

        if (!skip_proto_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return response;
}

std::optional<SteamPollAuthSessionStatusResponse> parse_poll_auth_session_status_protobuf(
    std::string_view bytes) {
    SteamPollAuthSessionStatusResponse response;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_proto_varint(bytes, offset, tag)) {
            return std::nullopt;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);

        if (field_number == 3 && wire_type == 2) {
            std::string_view value;
            if (!read_proto_length_delimited(bytes, offset, value)) {
                return std::nullopt;
            }
            response.refresh_token = std::string{value};
            continue;
        }

        if (field_number == 4 && wire_type == 2) {
            std::string_view value;
            if (!read_proto_length_delimited(bytes, offset, value)) {
                return std::nullopt;
            }
            response.access_token = std::string{value};
            continue;
        }

        if (field_number == 6 && wire_type == 2) {
            std::string_view value;
            if (!read_proto_length_delimited(bytes, offset, value)) {
                return std::nullopt;
            }
            response.account_name = std::string{value};
            continue;
        }

        if (field_number == 5 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_proto_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            response.had_remote_interaction = value != 0;
            continue;
        }

        if (!skip_proto_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return response;
}

std::optional<SteamGenerateAccessTokenForAppResponse>
parse_generate_access_token_for_app_protobuf(std::string_view bytes) {
    SteamGenerateAccessTokenForAppResponse response;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_proto_varint(bytes, offset, tag)) {
            return std::nullopt;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);

        if (field_number == 1 && wire_type == 2) {
            std::string_view value;
            if (!read_proto_length_delimited(bytes, offset, value)) {
                return std::nullopt;
            }
            response.access_token = std::string{value};
            continue;
        }

        if (field_number == 2 && wire_type == 2) {
            std::string_view value;
            if (!read_proto_length_delimited(bytes, offset, value)) {
                return std::nullopt;
            }
            response.refresh_token = std::string{value};
            continue;
        }

        if (!skip_proto_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return response;
}

} // namespace

std::vector<std::uint8_t> encode_begin_auth_session_request(
    const SteamBeginAuthSessionRequest& request) {
    return encode_begin_auth_session_request_impl(request);
}

std::vector<std::uint8_t> encode_poll_auth_session_status_request(
    const SteamPollAuthSessionStatusRequest& request) {
    return encode_poll_auth_session_status_request_impl(request);
}

std::vector<std::uint8_t> encode_generate_access_token_for_app_request(
    const SteamGenerateAccessTokenForAppRequest& request) {
    return encode_generate_access_token_for_app_request_impl(request);
}

std::string steam_login_platform_website_id(SteamLoginPlatformType platform_type) {
    SteamBeginAuthSessionRequest request;
    request.platform_type = platform_type;
    return describe_platform_context(request.platform_type, request.device_friendly_name)
        .website_id;
}

std::string steam_web_api_url_encode(std::string_view value) {
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

std::optional<SteamRsaPublicKey> parse_get_password_rsa_public_key_response(
    std::string_view json) {
    const auto modulus = find_json_string(json, "publickey_mod");
    const auto exponent = find_json_string(json, "publickey_exp");
    const auto timestamp = find_json_string(json, "timestamp");
    if (!modulus.has_value() || !exponent.has_value() || !timestamp.has_value()) {
        return std::nullopt;
    }

    const auto parsed_timestamp = parse_u64(*timestamp);
    if (!parsed_timestamp.has_value()) {
        return std::nullopt;
    }

    return SteamRsaPublicKey{
        std::string{*modulus},
        std::string{*exponent},
        *parsed_timestamp,
    };
}

std::optional<SteamBeginAuthSessionResponse> parse_begin_auth_session_response(
    std::string_view json) {
    if (!json.empty() && json.front() != '{' && json.front() != '[') {
        return parse_begin_auth_session_protobuf(json);
    }

    SteamBeginAuthSessionResponse response;

    const auto client_id = find_json_number_like(json, "client_id");
    if (client_id.has_value()) {
        const auto parsed = parse_u64(*client_id);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        response.client_id = *parsed;
    }

    auto steam_id = find_json_number_like(json, "steamid");
    if (!steam_id.has_value()) {
        steam_id = find_json_number_like(json, "steam_id");
    }
    if (steam_id.has_value()) {
        const auto parsed = parse_u64(*steam_id);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        response.steam_id = *parsed;
    }

    const auto interval = find_json_number_like(json, "interval");
    if (interval.has_value()) {
        const auto parsed = parse_double(*interval);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        response.interval_seconds = *parsed;
    }

    const auto extended_error = find_json_string(json, "extended_error_message");
    if (extended_error.has_value()) {
        response.extended_error_message = std::string{*extended_error};
    }

    const auto message = find_json_string(json, "message");
    if (response.extended_error_message.empty() && message.has_value()) {
        response.extended_error_message = std::string{*message};
    }

    const auto eresult = find_json_number_like(json, "eresult");
    if (eresult.has_value()) {
        const auto prefix = "Steam eresult " + std::string{*eresult};
        if (response.extended_error_message.empty()) {
            response.extended_error_message = prefix;
        } else {
            response.extended_error_message = prefix + ": " + response.extended_error_message;
        }
    }

    const auto request_id = find_json_string(json, "request_id");
    if (request_id.has_value()) {
        const auto decoded = base64_decode(*request_id);
        if (!decoded.has_value()) {
            return std::nullopt;
        }
        response.request_id = *decoded;
    }
    if (response.request_id.empty()) {
        if (response.extended_error_message.empty()) {
            response.extended_error_message =
                "BeginAuthSessionViaCredentials did not return a request_id";
            return std::nullopt;
        }
        return response;
    }

    std::size_t search_from = 0;
    while (true) {
        const auto position = json.find("\"confirmation_type\"", search_from);
        if (position == std::string_view::npos) {
            break;
        }

        const auto tail = json.substr(position);
        const auto confirmation_type = find_json_number_like(tail, "confirmation_type");
        if (!confirmation_type.has_value()) {
            break;
        }

        const auto parsed = parse_u64(*confirmation_type);
        if (parsed.has_value()) {
            response.allowed_confirmations.push_back(
                {map_confirmation_type(*parsed), ""});
        }

        search_from = position + 1;
    }

    return response;
}

std::optional<SteamPollAuthSessionStatusResponse> parse_poll_auth_session_status_response(
    std::string_view bytes) {
    if (!bytes.empty() && bytes.front() != '{' && bytes.front() != '[') {
        return parse_poll_auth_session_status_protobuf(bytes);
    }

    SteamPollAuthSessionStatusResponse response;

    const auto refresh_token = find_json_string(bytes, "refresh_token");
    if (refresh_token.has_value()) {
        response.refresh_token = std::string{*refresh_token};
    }

    const auto access_token = find_json_string(bytes, "access_token");
    if (access_token.has_value()) {
        response.access_token = std::string{*access_token};
    }

    const auto account_name = find_json_string(bytes, "account_name");
    if (account_name.has_value()) {
        response.account_name = std::string{*account_name};
    }

    return response;
}

std::optional<SteamGenerateAccessTokenForAppResponse>
parse_generate_access_token_for_app_response(std::string_view bytes) {
    if (!bytes.empty() && bytes.front() != '{' && bytes.front() != '[') {
        return parse_generate_access_token_for_app_protobuf(bytes);
    }

    SteamGenerateAccessTokenForAppResponse response;

    const auto access_token = find_json_string(bytes, "access_token");
    if (access_token.has_value()) {
        response.access_token = std::string{*access_token};
    }

    const auto refresh_token = find_json_string(bytes, "refresh_token");
    if (refresh_token.has_value()) {
        response.refresh_token = std::string{*refresh_token};
    }

    return response;
}

SteamTransportResponse<SteamRsaPublicKey>
SteamWebApiAuthenticationTransport::get_password_rsa_public_key(const std::string& account_name) {
    if (account_name.empty()) {
        return {{false, "account name is required"}, std::nullopt};
    }

    cauth::core::platform::HttpRequest request;
    request.url = std::string{kSteamApiBaseUrl} +
                  "/IAuthenticationService/GetPasswordRSAPublicKey/v1/?account_name=" +
                  steam_web_api_url_encode(account_name);
    const auto response = cauth::core::platform::perform_platform_http_request(request);
    if (!response.ok) {
        return {{false, response.error_message}, std::nullopt};
    }

    const auto body = cauth::core::platform::http_body_as_string(response);
    if (!body.has_value()) {
        return {{false, "failed to decode GetPasswordRSAPublicKey response body"}, std::nullopt};
    }

    const auto key = parse_get_password_rsa_public_key_response(*body);
    if (!key.has_value()) {
        return {{false, "failed to parse GetPasswordRSAPublicKey response"}, std::nullopt};
    }

    return {{true, ""}, key};
}

SteamTransportResponse<SteamBeginAuthSessionResponse>
SteamWebApiAuthenticationTransport::begin_auth_session_via_credentials(
    const SteamBeginAuthSessionRequest& request) {
    if (request.account_name.empty() || request.encrypted_password.empty() ||
        request.encryption_timestamp == 0) {
        return {{false, "account name, encrypted password, and timestamp are required"},
                std::nullopt};
    }

    const auto protobuf = encode_begin_auth_session_request_impl(request);
    std::ostringstream body;
    body << "input_protobuf_encoded="
         << steam_web_api_url_encode(base64_encode_bytes(protobuf));

    cauth::core::platform::HttpRequest http_request;
    http_request.method = cauth::core::platform::HttpMethod::Post;
    http_request.url =
        std::string{kSteamApiBaseUrl} + "/IAuthenticationService/BeginAuthSessionViaCredentials/v1/";
    http_request.content_type = "application/x-www-form-urlencoded";
    http_request.headers =
        describe_platform_context(request.platform_type, request.device_friendly_name).headers;
    const auto encoded_body = body.str();
    http_request.body.assign(encoded_body.begin(), encoded_body.end());

    const auto response = cauth::core::platform::perform_platform_http_request(http_request);
    if (!response.ok) {
        return {{false, response.error_message}, std::nullopt};
    }

    if (steam_auth_debug_enabled()) {
        std::cerr << "BeginAuthSessionViaCredentials response hex: "
                  << bytes_to_hex(std::string_view{
                         reinterpret_cast<const char*>(response.body.data()), response.body.size()})
                  << '\n';
    }

    const auto parsed = parse_begin_auth_session_response(
        std::string_view{reinterpret_cast<const char*>(response.body.data()), response.body.size()});
    if (!parsed.has_value()) {
        return {{false, "failed to parse BeginAuthSessionViaCredentials response"},
                std::nullopt};
    }

    return {{true, ""}, parsed};
}

SteamTransportResponse<SteamPollAuthSessionStatusResponse>
SteamWebApiAuthenticationTransport::poll_auth_session_status(
    const SteamPollAuthSessionStatusRequest& request) {
    if (request.client_id == 0 || request.request_id.empty()) {
        return {{false, "client_id and request_id are required"}, std::nullopt};
    }

    const auto protobuf = encode_poll_auth_session_status_request_impl(request);
    std::ostringstream body;
    body << "input_protobuf_encoded="
         << steam_web_api_url_encode(base64_encode_bytes(protobuf));

    cauth::core::platform::HttpRequest http_request;
    http_request.method = cauth::core::platform::HttpMethod::Post;
    http_request.url =
        std::string{kSteamApiBaseUrl} + "/IAuthenticationService/PollAuthSessionStatus/v1/";
    http_request.content_type = "application/x-www-form-urlencoded";
    http_request.headers =
        describe_platform_context(request.platform_type, {}).headers;
    const auto encoded_body = body.str();
    http_request.body.assign(encoded_body.begin(), encoded_body.end());

    const auto response = cauth::core::platform::perform_platform_http_request(http_request);
    if (!response.ok) {
        return {{false, response.error_message}, std::nullopt};
    }

    if (steam_auth_debug_enabled()) {
        std::cerr << "PollAuthSessionStatus response hex: "
                  << bytes_to_hex(std::string_view{
                         reinterpret_cast<const char*>(response.body.data()), response.body.size()})
                  << '\n';
    }

    const auto parsed = parse_poll_auth_session_status_response(
        std::string_view{reinterpret_cast<const char*>(response.body.data()), response.body.size()});
    if (!parsed.has_value()) {
        return {{false, "failed to parse PollAuthSessionStatus response"}, std::nullopt};
    }

    return {{true, ""}, parsed};
}

SteamTransportResponse<SteamGenerateAccessTokenForAppResponse>
SteamWebApiAuthenticationTransport::generate_access_token_for_app(
    const SteamGenerateAccessTokenForAppRequest& request) {
    if (request.steam_id == 0 || request.refresh_token.empty()) {
        return {{false, "steam_id and refresh_token are required"}, std::nullopt};
    }

    const auto protobuf = encode_generate_access_token_for_app_request_impl(request);
    std::ostringstream body;
    body << "input_protobuf_encoded="
         << steam_web_api_url_encode(base64_encode_bytes(protobuf));

    cauth::core::platform::HttpRequest http_request;
    http_request.method = cauth::core::platform::HttpMethod::Post;
    http_request.url =
        std::string{kSteamApiBaseUrl} + "/IAuthenticationService/GenerateAccessTokenForApp/v1/";
    http_request.content_type = "application/x-www-form-urlencoded";
    http_request.headers =
        describe_platform_context(request.platform_type, {}).headers;
    const auto encoded_body = body.str();
    http_request.body.assign(encoded_body.begin(), encoded_body.end());

    const auto response = cauth::core::platform::perform_platform_http_request(http_request);
    if (!response.ok) {
        return {{false, response.error_message}, std::nullopt};
    }

    if (steam_auth_debug_enabled()) {
        std::cerr << "GenerateAccessTokenForApp response hex: "
                  << bytes_to_hex(std::string_view{
                         reinterpret_cast<const char*>(response.body.data()), response.body.size()})
                  << '\n';
    }

    const auto parsed = parse_generate_access_token_for_app_response(
        std::string_view{reinterpret_cast<const char*>(response.body.data()), response.body.size()});
    if (!parsed.has_value()) {
        return {{false, "failed to parse GenerateAccessTokenForApp response"},
                std::nullopt};
    }

    return {{true, ""}, parsed};
}

} // namespace cauth::steam::auth
