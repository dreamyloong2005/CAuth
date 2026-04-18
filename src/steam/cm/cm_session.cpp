#include "steam/cm/cm_session.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include "steam/cm/cm_client_hello.hpp"
#include "steam/cm/cm_heartbeat.hpp"

namespace cauth::core::cm {
namespace {

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

void append_fixed64_field(std::vector<std::uint8_t>& out, int field_number, std::uint64_t value) {
    append_tag(out, field_number, 1);
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_varint_field(std::vector<std::uint8_t>& out, int field_number, std::uint64_t value) {
    append_tag(out, field_number, 0);
    append_varint(out, value);
}

bool read_varint(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                 std::uint64_t& value) {
    value = 0;
    int shift = 0;
    while (offset < bytes.size() && shift <= 63) {
        const auto byte = bytes[offset++];
        value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            return true;
        }
        shift += 7;
    }
    return false;
}

bool skip_field(const std::vector<std::uint8_t>& bytes, std::size_t& offset, int wire_type) {
    std::uint64_t ignored = 0;
    switch (wire_type) {
    case 0:
        return read_varint(bytes, offset, ignored);
    case 1:
        if (bytes.size() - offset < 8) {
            return false;
        }
        offset += 8;
        return true;
    case 2:
        if (!read_varint(bytes, offset, ignored) || bytes.size() - offset < ignored) {
            return false;
        }
        offset += static_cast<std::size_t>(ignored);
        return true;
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

struct CmProtoHeaderSession {
    std::uint64_t steam_id = 0;
    std::uint32_t client_session_id = 0;
};

CmProtoHeaderSession parse_proto_header_session(const std::vector<std::uint8_t>& header) {
    CmProtoHeaderSession parsed;
    std::size_t offset = 0;
    while (offset < header.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(header, offset, tag)) {
            return parsed;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);

        if (field_number == 1 && wire_type == 1) {
            if (header.size() - offset < 8) {
                return parsed;
            }
            std::uint64_t value = 0;
            for (int shift = 0; shift < 64; shift += 8) {
                value |= static_cast<std::uint64_t>(header[offset++]) << shift;
            }
            parsed.steam_id = value;
            continue;
        }

        if (field_number == 2 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(header, offset, value)) {
                return parsed;
            }
            parsed.client_session_id = static_cast<std::uint32_t>(value);
            continue;
        }

        if (!skip_field(header, offset, wire_type)) {
            return parsed;
        }
    }

    return parsed;
}

std::vector<std::uint8_t> make_session_header(std::uint64_t steam_id,
                                              std::uint32_t client_session_id,
                                              const std::vector<std::uint8_t>& existing) {
    std::vector<std::uint8_t> header = existing;
    if (steam_id != 0) {
        append_fixed64_field(header, 1, steam_id);
    }
    if (client_session_id != 0) {
        append_varint_field(header, 2, client_session_id);
    }
    return header;
}

} // namespace

CmSession::CmSession(std::unique_ptr<CmWebSocketConnection> connection)
    : connection_(std::move(connection)) {}

CmSession::~CmSession() {
    close();
}

CmSessionConnectResult CmSession::logon(const session::AuthSession& session,
                                        const CmLogonRequest& request,
                                        int max_receive_attempts) {
    if (!connection_) {
        return {false, "CM connection is not open", {}};
    }

    const auto hello = make_client_hello_message();
    const auto hello_result = connection_->send_binary(encode_cm_message(hello));
    if (!hello_result.ok) {
        return {false, "ClientHello send failed: " + hello_result.error_message, {}};
    }

    const auto logon_message = make_client_logon_message(session, request);
    const auto send_result = connection_->send_binary(encode_cm_message(logon_message));
    if (!send_result.ok) {
        return {false, "ClientLogon send failed: " + send_result.error_message, {}};
    }

    for (int receive_attempt = 0; receive_attempt < max_receive_attempts; ++receive_attempt) {
        const auto received = connection_->receive();
        if (!received.ok) {
            return {false, "ClientLogon receive failed: " + received.error_message, {}};
        }

        const auto decoded = decode_cm_message(received.bytes);
        if (!decoded.has_value()) {
            return {false, "CM message decode failed", {}};
        }

        std::string unpack_error;
        const auto messages = unpack_cm_messages(*decoded, &unpack_error);
        if (messages.empty() && !unpack_error.empty()) {
            return {false, "CM multi unpack failed: " + unpack_error, {}};
        }

        for (const auto& message : messages) {
            if (message.emsg != EMsg::ClientLogOnResponse) {
                continue;
            }

            const auto logon_response = parse_client_logon_response_body(message.body);
            if (!logon_response.has_value()) {
                return {false, "ClientLogon response body parse failed", {}};
            }

            if (!logon_response->ok) {
                return {false, "CM logon failed", *logon_response};
            }

            const auto session_header = parse_proto_header_session(message.header);
            steam_id_ = session_header.steam_id != 0 ? session_header.steam_id
                                                     : cauth::steam::auth::steam_id(session);
            client_session_id_ = session_header.client_session_id;

            return {true, "", *logon_response};
        }
    }

    return {false, "ClientLogon response not received", {}};
}

CmWebSocketProbeResult CmSession::send_heartbeat(const session::AuthSession& session) {
    if (!connection_) {
        return {false, "CM connection is not open"};
    }

    return connection_->send_binary(encode_cm_message(make_client_heartbeat_message(session)));
}

CmWebSocketProbeResult CmSession::send_message(const CmMessage& message) {
    if (!connection_) {
        return {false, "CM connection is not open"};
    }

    auto authed_message = message;
    authed_message.header = make_session_header(steam_id_, client_session_id_, message.header);
    return connection_->send_binary(encode_cm_message(authed_message));
}

CmSessionReceiveResult CmSession::receive_messages(int max_receive_attempts) {
    if (!connection_) {
        return {false, "CM connection is not open", {}};
    }

    for (int receive_attempt = 0; receive_attempt < max_receive_attempts; ++receive_attempt) {
        const auto received = connection_->receive();
        if (!received.ok) {
            return {false, received.error_message, {}};
        }

        const auto decoded = decode_cm_message(received.bytes);
        if (!decoded.has_value()) {
            return {false, "CM message decode failed", {}};
        }

        std::string unpack_error;
        auto messages = unpack_cm_messages(*decoded, &unpack_error);
        if (messages.empty() && !unpack_error.empty()) {
            return {false, "CM multi unpack failed: " + unpack_error, {}};
        }

        if (!messages.empty()) {
            return {true, "", std::move(messages)};
        }
    }

    return {false, "CM message not received", {}};
}

CmServiceMethodCallResult CmSession::call_service_method(std::string_view target_job_name,
                                                         const std::vector<std::uint8_t>& body,
                                                         std::uint64_t job_id_source,
                                                         int max_receive_attempts) {
    if (!connection_) {
        return {false, "CM connection is not open", {}, {}};
    }

    CmServiceMethodClient service_client{*connection_};
    auto message = make_service_method_call(target_job_name, body, job_id_source);
    message.header = make_session_header(steam_id_, client_session_id_, message.header);
    return service_client.call_with_message(message, job_id_source, max_receive_attempts);
}

void CmSession::close() {
    if (connection_) {
        connection_->close();
        connection_.reset();
    }
}

} // namespace cauth::core::cm
