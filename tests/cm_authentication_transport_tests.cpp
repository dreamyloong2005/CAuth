#include "steam/auth/cm_authentication_transport.hpp"
#include "steam/cm/cm_message.hpp"

#include <deque>
#include <iostream>
#include <string>

class FakeConnection final : public cauth::core::cm::CmWebSocketConnection {
  public:
    cauth::core::cm::CmWebSocketProbeResult
    send_binary(const std::vector<std::uint8_t>& bytes) override {
        sent_messages.push_back(bytes);
        return {true, ""};
    }

    cauth::core::cm::CmWebSocketReceiveResult receive() override {
        if (responses.empty()) {
            return {false, "no fake response", {}};
        }

        auto response = responses.front();
        responses.pop_front();
        return response;
    }

    void close() override {}

    std::vector<std::vector<std::uint8_t>> sent_messages;
    std::deque<cauth::core::cm::CmWebSocketReceiveResult> responses;
};

std::vector<std::uint8_t> service_response_header(std::uint64_t job_id_target) {
    std::vector<std::uint8_t> header{
        0x59,
        static_cast<std::uint8_t>(job_id_target & 0xffU),
        static_cast<std::uint8_t>((job_id_target >> 8) & 0xffU),
        static_cast<std::uint8_t>((job_id_target >> 16) & 0xffU),
        static_cast<std::uint8_t>((job_id_target >> 24) & 0xffU),
        static_cast<std::uint8_t>((job_id_target >> 32) & 0xffU),
        static_cast<std::uint8_t>((job_id_target >> 40) & 0xffU),
        static_cast<std::uint8_t>((job_id_target >> 48) & 0xffU),
        static_cast<std::uint8_t>((job_id_target >> 56) & 0xffU),
        0x68,
        0x01,
    };
    return header;
}

std::vector<std::uint8_t> service_response(std::uint64_t job_id_target,
                                           const std::vector<std::uint8_t>& body) {
    return cauth::core::cm::encode_cm_message(cauth::core::cm::CmMessage{
        cauth::core::cm::EMsg::ServiceMethodResponse,
        true,
        service_response_header(job_id_target),
        body,
    });
}

int main() {
    FakeConnection connection;
    cauth::core::cm::CmServiceMethodClient service_client{connection};
    cauth::steam::auth::CmAuthenticationTransport transport{service_client};

    const std::vector<std::uint8_t> begin_body{
        0x08, 0x2a,
        0x12, 0x03, 0x01, 0x02, 0x03,
        0x1d, 0x00, 0x00, 0xa0, 0x40,
        0x28, 0x63,
    };
    connection.responses.push_back({true, "", service_response(1, begin_body)});

    const auto begin = transport.begin_auth_session_via_credentials(
        cauth::steam::auth::SteamBeginAuthSessionRequest{
            "account",
            "encrypted-password",
            123,
            "",
            "CAuth",
            true,
            cauth::steam::auth::SteamLoginPlatformType::SteamClient,
        });
    if (!begin.result.ok || !begin.value.has_value() || begin.value->client_id != 42 ||
        begin.value->request_id.size() != 3 || begin.value->steam_id != 99 ||
        connection.sent_messages.size() != 1) {
        std::cerr << "CM auth transport should call begin auth via service method\n";
        return 1;
    }

    const auto sent_begin = cauth::core::cm::decode_cm_message(connection.sent_messages[0]);
    if (!sent_begin.has_value() ||
        sent_begin->emsg != cauth::core::cm::EMsg::ServiceMethodCallFromClientNonAuthed) {
        std::cerr << "CM auth transport should send non-authed service method call\n";
        return 1;
    }

    std::vector<std::uint8_t> poll_body;
    const std::string refresh_token = "refresh-token";
    const std::string access_token = "access-token";
    const std::string account_name = "account";
    poll_body.push_back(0x1a);
    poll_body.push_back(static_cast<std::uint8_t>(refresh_token.size()));
    poll_body.insert(poll_body.end(), refresh_token.begin(), refresh_token.end());
    poll_body.push_back(0x22);
    poll_body.push_back(static_cast<std::uint8_t>(access_token.size()));
    poll_body.insert(poll_body.end(), access_token.begin(), access_token.end());
    poll_body.push_back(0x32);
    poll_body.push_back(static_cast<std::uint8_t>(account_name.size()));
    poll_body.insert(poll_body.end(), account_name.begin(), account_name.end());
    connection.responses.push_back({true, "", service_response(2, poll_body)});

    const auto poll = transport.poll_auth_session_status(
        cauth::steam::auth::SteamPollAuthSessionStatusRequest{42, {1, 2, 3}});
    if (!poll.result.ok || !poll.value.has_value() ||
        poll.value->refresh_token != "refresh-token" ||
        poll.value->access_token != "access-token" ||
        poll.value->account_name != "account" || connection.sent_messages.size() != 2) {
        std::cerr << "CM auth transport should call poll auth via service method\n";
        return 1;
    }

    return 0;
}
