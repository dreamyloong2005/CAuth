#include "steam/cm/cm_message.hpp"
#include "steam/cm/cm_service_method.hpp"

#include <algorithm>
#include <deque>
#include <iostream>

namespace {

bool contains_bytes(const std::vector<std::uint8_t>& haystack,
                    const std::vector<std::uint8_t>& needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) !=
           haystack.end();
}

} // namespace

class FakeConnection final : public cauth::core::cm::CmWebSocketConnection {
  public:
    cauth::core::cm::CmWebSocketProbeResult
    send_binary(const std::vector<std::uint8_t>& bytes) override {
        sent_messages.push_back(bytes);
        return send_result;
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

    cauth::core::cm::CmWebSocketProbeResult send_result{true, ""};
    std::vector<std::vector<std::uint8_t>> sent_messages;
    std::deque<cauth::core::cm::CmWebSocketReceiveResult> responses;
    bool closed = false;
};

int main() {
    const std::vector<std::uint8_t> body{1, 2, 3};
    const std::string target_job_name = "Authentication.BeginAuthSessionViaCredentials#1";
    const auto message = cauth::core::cm::make_non_authed_service_method_call(
        target_job_name, body, 0x1122334455667788ULL);

    if (message.emsg != cauth::core::cm::EMsg::ServiceMethodCallFromClientNonAuthed ||
        !message.protobuf || message.body != body) {
        std::cerr << "service method call should use non-authed service EMsg and keep body\n";
        return 1;
    }

    std::vector<std::uint8_t> expected_target{0x62,
                                              static_cast<std::uint8_t>(target_job_name.size())};
    expected_target.insert(expected_target.end(), target_job_name.begin(), target_job_name.end());
    if (!contains_bytes(message.header, {0x51, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}) ||
        !contains_bytes(message.header, expected_target)) {
        std::cerr << "service method header should include job id and target job name\n";
        return 1;
    }

    const auto encoded = cauth::core::cm::encode_cm_message(message);
    const auto decoded = cauth::core::cm::decode_cm_message(encoded);
    if (!decoded.has_value() ||
        decoded->emsg != cauth::core::cm::EMsg::ServiceMethodCallFromClientNonAuthed ||
        decoded->header != message.header || decoded->body != body) {
        std::cerr << "service method CM message should round-trip through framing\n";
        return 1;
    }

    const std::vector<std::uint8_t> response_header{
        0x59, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x68, 0x01,
        0x72, 0x02, 'o',  'k',
    };
    const auto parsed = cauth::core::cm::parse_service_method_response_header(response_header);
    if (!parsed.has_value() || parsed->job_id_target != 0x1122334455667788ULL ||
        parsed->eresult != 1 || parsed->error_message != "ok") {
        std::cerr << "service method response header should parse job, eresult, and error\n";
        return 1;
    }

    FakeConnection connection;
    connection.responses.push_back(cauth::core::cm::CmWebSocketReceiveResult{
        true,
        "",
        cauth::core::cm::encode_cm_message(cauth::core::cm::CmMessage{
            cauth::core::cm::EMsg::ServiceMethodResponse,
            true,
            response_header,
            {9, 8, 7},
        }),
    });

    cauth::core::cm::CmServiceMethodClient client{connection};
    const auto result = client.call_non_authed(target_job_name, body, 0x1122334455667788ULL);
    if (!result.ok || result.body != std::vector<std::uint8_t>{9, 8, 7} ||
        connection.sent_messages.empty()) {
        std::cerr << "service method client should send request and return matching response\n";
        return 1;
    }

    const auto sent = cauth::core::cm::decode_cm_message(connection.sent_messages.front());
    if (!sent.has_value() ||
        sent->emsg != cauth::core::cm::EMsg::ServiceMethodCallFromClientNonAuthed ||
        sent->body != body) {
        std::cerr << "service method client should send encoded non-authed call\n";
        return 1;
    }

    FakeConnection authed_connection;
    authed_connection.responses.push_back(cauth::core::cm::CmWebSocketReceiveResult{
        true,
        "",
        cauth::core::cm::encode_cm_message(cauth::core::cm::CmMessage{
            cauth::core::cm::EMsg::ServiceMethodResponse,
            true,
            response_header,
            {3, 2, 1},
        }),
    });
    cauth::core::cm::CmServiceMethodClient authed_client{authed_connection};
    const auto authed_result =
        authed_client.call(target_job_name, body, 0x1122334455667788ULL);
    const auto authed_sent =
        cauth::core::cm::decode_cm_message(authed_connection.sent_messages.front());
    if (!authed_result.ok || authed_result.body != std::vector<std::uint8_t>{3, 2, 1} ||
        !authed_sent.has_value() ||
        authed_sent->emsg != cauth::core::cm::EMsg::ServiceMethodCallFromClient) {
        std::cerr << "service method client should send authed service method calls\n";
        return 1;
    }

    FakeConnection response_without_job_connection;
    const std::vector<std::uint8_t> response_without_job_header{0x68, 0x01};
    response_without_job_connection.responses.push_back(cauth::core::cm::CmWebSocketReceiveResult{
        true,
        "",
        cauth::core::cm::encode_cm_message(cauth::core::cm::CmMessage{
            cauth::core::cm::EMsg::ServiceMethodResponse,
            true,
            response_without_job_header,
            {6, 5, 4},
        }),
    });
    cauth::core::cm::CmServiceMethodClient response_without_job_client{
        response_without_job_connection};
    const auto response_without_job_result =
        response_without_job_client.call_non_authed(target_job_name, body, 0x1122334455667788ULL);
    if (!response_without_job_result.ok ||
        response_without_job_result.body != std::vector<std::uint8_t>{6, 5, 4}) {
        std::cerr << "service method client should accept service responses without job target\n";
        return 1;
    }

    return 0;
}
