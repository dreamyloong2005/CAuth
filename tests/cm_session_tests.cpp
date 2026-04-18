#include "core/session/auth_session.hpp"
#include "steam/auth/steam_session_identity.hpp"
#include "steam/cm/cm_message.hpp"
#include "steam/cm/cm_session.hpp"

#include <algorithm>
#include <deque>
#include <iostream>

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

    void close() override { closed = true; }

    std::vector<std::vector<std::uint8_t>> sent_messages;
    std::deque<cauth::core::cm::CmWebSocketReceiveResult> responses;
    bool closed = false;
};

bool contains_bytes(const std::vector<std::uint8_t>& haystack,
                    const std::vector<std::uint8_t>& needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) !=
           haystack.end();
}

int main() {
    auto fake = std::make_unique<FakeConnection>();
    auto* fake_ptr = fake.get();

    fake->responses.push_back(cauth::core::cm::CmWebSocketReceiveResult{
        true,
        "",
        cauth::core::cm::encode_cm_message(cauth::core::cm::CmMessage{
            cauth::core::cm::EMsg::ClientLogOnResponse,
            true,
            {0x09, 0x00, 0x3d, 0x9a, 0xd0, 0x2f, 0x00, 0x10, 0x01, 0x10, 0x7b},
            {0x08, 0x01, 0x18, 0x09},
        }),
    });

    cauth::core::cm::CmSession cm_session{std::move(fake)};
    const cauth::core::session::AuthSession auth_session{
        std::string{cauth::steam::auth::kSteamAuthProvider},
        "76561198000000000",
        "account",
        "refresh-token",
        "access-token",
    };

    const auto logon = cm_session.logon(auth_session);
    if (!logon.ok || logon.logon_response.eresult != 1 ||
        logon.logon_response.heartbeat_seconds != 9 || fake_ptr->sent_messages.size() != 2) {
        std::cerr << "CM session should send hello and logon, then parse logon response\n";
        return 1;
    }

    const auto hello = cauth::core::cm::decode_cm_message(fake_ptr->sent_messages[0]);
    const auto logon_message = cauth::core::cm::decode_cm_message(fake_ptr->sent_messages[1]);
    if (!hello.has_value() || hello->emsg != cauth::core::cm::EMsg::ClientHello ||
        !logon_message.has_value() ||
        logon_message->emsg != cauth::core::cm::EMsg::ClientLogon) {
        std::cerr << "CM session should send ClientHello before ClientLogon\n";
        return 1;
    }

    const auto heartbeat = cm_session.send_heartbeat(auth_session);
    if (!heartbeat.ok || fake_ptr->sent_messages.size() != 3) {
        std::cerr << "CM session should send heartbeat over the active connection\n";
        return 1;
    }

    const cauth::core::cm::CmMessage pics_request{
        cauth::core::cm::EMsg::ClientPICSProductInfoRequest,
        true,
        {},
        {1, 2, 3},
    };
    const auto generic_send = cm_session.send_message(pics_request);
    fake_ptr->responses.push_back(cauth::core::cm::CmWebSocketReceiveResult{
        true,
        "",
        cauth::core::cm::encode_cm_message(cauth::core::cm::CmMessage{
            cauth::core::cm::EMsg::ClientPICSProductInfoResponse,
            true,
            {},
            {4, 5, 6},
        }),
    });
    const auto generic_receive = cm_session.receive_messages();
    const auto generic_sent = cauth::core::cm::decode_cm_message(fake_ptr->sent_messages.back());
    if (!generic_send.ok || !generic_receive.ok || generic_receive.messages.size() != 1 ||
        generic_receive.messages[0].emsg !=
            cauth::core::cm::EMsg::ClientPICSProductInfoResponse ||
        generic_receive.messages[0].body != std::vector<std::uint8_t>{4, 5, 6} ||
        !generic_sent.has_value() ||
        !contains_bytes(generic_sent->header,
                        {0x09, 0x00, 0x3d, 0x9a, 0xd0, 0x2f, 0x00, 0x10, 0x01}) ||
        !contains_bytes(generic_sent->header, {0x10, 0x7b})) {
        std::cerr << "CM session should support generic CM message send and receive\n";
        return 1;
    }

    const std::vector<std::uint8_t> service_response_header{
        0x59, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x68, 0x01,
    };
    fake_ptr->responses.push_back(cauth::core::cm::CmWebSocketReceiveResult{
        true,
        "",
        cauth::core::cm::encode_cm_message(cauth::core::cm::CmMessage{
            cauth::core::cm::EMsg::ServiceMethodResponse,
            true,
            service_response_header,
            {7, 7},
        }),
    });

    const auto service_result = cm_session.call_service_method("Example.Method#1", {1}, 4);
    const auto service_message = cauth::core::cm::decode_cm_message(fake_ptr->sent_messages.back());
    if (!service_result.ok || service_result.body != std::vector<std::uint8_t>{7, 7} ||
        !service_message.has_value() ||
        service_message->emsg != cauth::core::cm::EMsg::ServiceMethodCallFromClient ||
        !contains_bytes(service_message->header,
                        {0x09, 0x00, 0x3d, 0x9a, 0xd0, 0x2f, 0x00, 0x10, 0x01}) ||
        !contains_bytes(service_message->header, {0x10, 0x7b})) {
        std::cerr << "CM session should send authed service method calls\n";
        return 1;
    }

    cm_session.close();
    if (!fake_ptr->closed) {
        std::cerr << "CM session close should close the underlying connection\n";
        return 1;
    }

    return 0;
}
