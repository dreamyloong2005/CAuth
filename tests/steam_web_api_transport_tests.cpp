#include "steam/auth/steam_web_api_auth_transport.hpp"

#include <iostream>

int main() {
    const auto encoded = cauth::steam::auth::steam_web_api_url_encode("name+with space@example.com");
    if (encoded != "name%2Bwith%20space%40example.com") {
        std::cerr << "URL encoding should escape reserved characters\n";
        return 1;
    }

    const auto parsed = cauth::steam::auth::parse_get_password_rsa_public_key_response(
        R"({"response":{"publickey_mod":"abcdef","publickey_exp":"010001","timestamp":"12345"}})");
    if (!parsed.has_value() || parsed->modulus_hex != "abcdef" ||
        parsed->exponent_hex != "010001" || parsed->timestamp != 12345) {
        std::cerr << "RSA key response should parse expected fields\n";
        return 1;
    }

    const auto bad = cauth::steam::auth::parse_get_password_rsa_public_key_response(
        R"({"response":{"publickey_mod":"abcdef","publickey_exp":"010001","timestamp":"bad"}})");
    if (bad.has_value()) {
        std::cerr << "RSA key parser should reject invalid timestamps\n";
        return 1;
    }

    const auto begin = cauth::steam::auth::parse_begin_auth_session_response(
        R"({"response":{"client_id":"42","request_id":"AQID","steamid":"76561198000000000","interval":5,"allowed_confirmations":[{"confirmation_type":2}]}})");
    if (!begin.has_value() || begin->client_id != 42 || begin->request_id.size() != 3 ||
        begin->request_id[0] != 1 || begin->steam_id != 76561198000000000ULL ||
        begin->interval_seconds != 5.0 || begin->allowed_confirmations.empty() ||
        begin->allowed_confirmations[0].type !=
            cauth::steam::auth::SteamGuardConfirmationType::EmailCode) {
        std::cerr << "begin auth response should parse poll fields and confirmations\n";
        return 1;
    }

    const auto malformed_begin = cauth::steam::auth::parse_begin_auth_session_response(
        R"({"response":{"client_id":"not-a-number","request_id":"AQID"}})");
    if (malformed_begin.has_value()) {
        std::cerr << "begin auth parser should reject invalid client_id\n";
        return 1;
    }

    const auto rejected_begin = cauth::steam::auth::parse_begin_auth_session_response(
        R"({"response":{"eresult":5,"message":"Invalid password"}})");
    if (!rejected_begin.has_value() ||
        rejected_begin->extended_error_message != "Steam eresult 5: Invalid password") {
        std::cerr << "begin auth parser should preserve Steam error context\n";
        return 1;
    }

    const std::string protobuf_begin{
        "\x08\x2a"
        "\x12\x03\x01\x02\x03"
        "\x1d\x00\x00\xa0\x40"
        "\x22\x02\x08\x02"
        "\x28\x63"
        "\x42\x00",
        20,
    };
    const auto parsed_protobuf =
        cauth::steam::auth::parse_begin_auth_session_response(protobuf_begin);
    if (!parsed_protobuf.has_value() || parsed_protobuf->client_id != 42 ||
        parsed_protobuf->request_id.size() != 3 ||
        parsed_protobuf->interval_seconds != 5.0 || parsed_protobuf->steam_id != 99 ||
        parsed_protobuf->allowed_confirmations.empty() ||
        parsed_protobuf->allowed_confirmations[0].type !=
            cauth::steam::auth::SteamGuardConfirmationType::EmailCode) {
        std::cerr << "begin auth protobuf response should parse expected fields\n";
        return 1;
    }

    std::string poll_protobuf;
    poll_protobuf.append("\x1a\x0d", 2);
    poll_protobuf.append("refresh-token", 13);
    poll_protobuf.append("\x22\x0c", 2);
    poll_protobuf.append("access-token", 12);
    poll_protobuf.append("\x32\x0c", 2);
    poll_protobuf.append("test_account", 12);
    poll_protobuf.append("\x28\x01", 2);
    const auto parsed_poll =
        cauth::steam::auth::parse_poll_auth_session_status_response(poll_protobuf);
    if (!parsed_poll.has_value() || parsed_poll->refresh_token != "refresh-token" ||
        parsed_poll->access_token != "access-token" ||
        parsed_poll->account_name != "test_account" ||
        !parsed_poll->had_remote_interaction) {
        std::cerr << "poll auth protobuf response should parse token fields\n";
        return 1;
    }

    const auto parsed_poll_json = cauth::steam::auth::parse_poll_auth_session_status_response(
        R"({"response":{"refresh_token":"refresh","access_token":"access","account_name":"name"}})");
    if (!parsed_poll_json.has_value() || parsed_poll_json->refresh_token != "refresh" ||
        parsed_poll_json->access_token != "access" ||
        parsed_poll_json->account_name != "name") {
        std::cerr << "poll auth JSON response should parse token fields\n";
        return 1;
    }

    std::string access_token_protobuf;
    access_token_protobuf.append("\x0a\x0c", 2);
    access_token_protobuf.append("access-token", 12);
    access_token_protobuf.append("\x12\x0d", 2);
    access_token_protobuf.append("refresh-token", 13);

    const auto parsed_access =
        cauth::steam::auth::parse_generate_access_token_for_app_response(access_token_protobuf);
    if (!parsed_access.has_value() || parsed_access->access_token != "access-token" ||
        parsed_access->refresh_token != "refresh-token") {
        std::cerr << "generate access token protobuf response should parse token fields\n";
        return 1;
    }

    const auto parsed_access_json =
        cauth::steam::auth::parse_generate_access_token_for_app_response(
            R"({"response":{"access_token":"access","refresh_token":"refresh"}})");
    if (!parsed_access_json.has_value() || parsed_access_json->access_token != "access" ||
        parsed_access_json->refresh_token != "refresh") {
        std::cerr << "generate access token JSON response should parse token fields\n";
        return 1;
    }

    return 0;
}
