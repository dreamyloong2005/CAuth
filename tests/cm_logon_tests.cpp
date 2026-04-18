#include "core/session/auth_session.hpp"
#include "steam/auth/steam_session_identity.hpp"
#include "steam/cm/cm_logon.hpp"
#include "steam/cm/cm_message.hpp"

#include <algorithm>
#include <iostream>

namespace {

bool contains_bytes(const std::vector<std::uint8_t>& haystack,
                    const std::vector<std::uint8_t>& needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) !=
           haystack.end();
}

} // namespace

int main() {
    const cauth::core::session::AuthSession session{
        std::string{cauth::steam::auth::kSteamAuthProvider},
        "76561198000000000",
        "test_account",
        "refresh-token",
        "access-token",
    };

    const auto message = cauth::core::cm::make_client_logon_message(
        session, cauth::core::cm::CmLogonRequest{});
    if (message.emsg != cauth::core::cm::EMsg::ClientLogon || !message.protobuf ||
        message.header.empty() || message.body.empty()) {
        std::cerr << "client logon message should include proto header and body\n";
        return 1;
    }

    if (!contains_bytes(message.header,
                        {0x09, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x10, 0x01})) {
        std::cerr << "client logon header should use the pre-logon individual SteamID\n";
        return 1;
    }

    if (!contains_bytes(message.body, {0x28}) ||
        !contains_bytes(message.body, {0x10, 0x8d, 0xe0, 0xb7, 0xd5, 0x0b}) ||
        !contains_bytes(message.body, {0x5a, 0x05, 0x0d, 0x0d, 0xf0, 0xad, 0xba}) ||
        !contains_bytes(message.body, {0xf2, 0x01}) ||
        !contains_bytes(message.body, {'B', 'B', '3', 0}) ||
        !contains_bytes(message.body, {0xb0, 0x06, 0x01}) ||
        !contains_bytes(message.body, {0xe2, 0x06, 0x0d})) {
        std::cerr << "client logon body should include package, obfuscated-ip, machine-id, "
                     "rate-limit, and access-token fields\n";
        return 1;
    }

    if (!contains_bytes(message.body, {'r', 'e', 'f', 'r', 'e', 's', 'h', '-', 't', 'o', 'k',
                                       'e', 'n'}) ||
        contains_bytes(message.body, {'a', 'c', 'c', 'e', 's', 's', '-', 't', 'o', 'k', 'e',
                                      'n'})) {
        std::cerr << "client logon should place the refresh token in the access_token field\n";
        return 1;
    }

    const auto encoded = cauth::core::cm::encode_cm_message(message);
    const auto decoded = cauth::core::cm::decode_cm_message(encoded);
    if (!decoded.has_value() || decoded->emsg != cauth::core::cm::EMsg::ClientLogon ||
        decoded->body != message.body) {
        std::cerr << "client logon CM message should round-trip through framing\n";
        return 1;
    }

    const std::vector<std::uint8_t> response_body{0x08, 0x01, 0x10, 0x1e};
    const auto response = cauth::core::cm::parse_client_logon_response_body(response_body);
    if (!response.has_value() || !response->ok || response->eresult != 1 ||
        response->heartbeat_seconds != 30) {
        std::cerr << "client logon response should parse eresult and heartbeat\n";
        return 1;
    }

    return 0;
}
