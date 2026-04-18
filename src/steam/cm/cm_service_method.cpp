#include "steam/cm/cm_service_method.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

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

void append_string_field(std::vector<std::uint8_t>& out, int field_number, std::string_view value) {
    if (value.empty()) {
        return;
    }

    append_tag(out, field_number, 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
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

bool read_fixed64(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                  std::uint64_t& value) {
    if (bytes.size() - offset < 8) {
        return false;
    }

    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    }
    return true;
}

bool read_length_delimited(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                           std::string& value) {
    std::uint64_t length = 0;
    if (!read_varint(bytes, offset, length) || bytes.size() - offset < length) {
        return false;
    }

    value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += static_cast<std::size_t>(length);
    return true;
}

bool skip_field(const std::vector<std::uint8_t>& bytes, std::size_t& offset, int wire_type) {
    std::uint64_t ignored = 0;
    std::string ignored_string;
    switch (wire_type) {
    case 0:
        return read_varint(bytes, offset, ignored);
    case 1:
        return read_fixed64(bytes, offset, ignored);
    case 2:
        return read_length_delimited(bytes, offset, ignored_string);
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

bool debug_cm_enabled() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t value_length = 0;
    if (_dupenv_s(&value, &value_length, "CAUTH_DEBUG_CM") != 0 || value == nullptr) {
        return false;
    }

    const std::string debug_value{value};
    std::free(value);
    return value_length > 0 && debug_value == "1";
#else
    const auto* value = std::getenv("CAUTH_DEBUG_CM");
    return value != nullptr && std::string_view{value} == "1";
#endif
}

const char* service_method_eresult_name(std::uint32_t eresult) {
    switch (eresult) {
    case 8:
        return "InvalidParam";
    case 5:
        return "InvalidPassword";
    case 84:
        return "RateLimitExceeded";
    case 87:
        return "AccountLoginDeniedThrottle";
    default:
        return "";
    }
}

} // namespace

CmMessage make_service_method_call_with_emsg(EMsg emsg, std::string_view target_job_name,
                                             const std::vector<std::uint8_t>& body,
                                             std::uint64_t job_id_source) {
    std::vector<std::uint8_t> header;
    append_fixed64_field(header, 10, job_id_source);
    append_string_field(header, 12, target_job_name);

    return CmMessage{
        emsg,
        true,
        std::move(header),
        body,
    };
}

CmMessage make_non_authed_service_method_call(std::string_view target_job_name,
                                              const std::vector<std::uint8_t>& body,
                                              std::uint64_t job_id_source) {
    return make_service_method_call_with_emsg(EMsg::ServiceMethodCallFromClientNonAuthed,
                                              target_job_name, body, job_id_source);
}

CmMessage make_service_method_call(std::string_view target_job_name,
                                   const std::vector<std::uint8_t>& body,
                                   std::uint64_t job_id_source) {
    return make_service_method_call_with_emsg(EMsg::ServiceMethodCallFromClient, target_job_name,
                                              body, job_id_source);
}

std::optional<CmServiceMethodResponseHeader>
parse_service_method_response_header(const std::vector<std::uint8_t>& header) {
    CmServiceMethodResponseHeader parsed;
    std::size_t offset = 0;
    while (offset < header.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(header, offset, tag)) {
            return std::nullopt;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);

        if (field_number == 11 && wire_type == 1) {
            if (!read_fixed64(header, offset, parsed.job_id_target)) {
                return std::nullopt;
            }
            continue;
        }

        if (field_number == 13 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(header, offset, value)) {
                return std::nullopt;
            }
            parsed.eresult = static_cast<std::uint32_t>(value);
            continue;
        }

        if (field_number == 14 && wire_type == 2) {
            if (!read_length_delimited(header, offset, parsed.error_message)) {
                return std::nullopt;
            }
            continue;
        }

        if (!skip_field(header, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return parsed;
}

CmServiceMethodClient::CmServiceMethodClient(CmWebSocketConnection& connection)
    : connection_(connection) {}

CmServiceMethodCallResult CmServiceMethodClient::call_non_authed(
    std::string_view target_job_name, const std::vector<std::uint8_t>& body,
    std::uint64_t job_id_source, int max_receive_attempts) {
    const auto message =
        make_non_authed_service_method_call(target_job_name, body, job_id_source);
    return call_with_message(message, job_id_source, max_receive_attempts);
}

CmServiceMethodCallResult CmServiceMethodClient::call(
    std::string_view target_job_name, const std::vector<std::uint8_t>& body,
    std::uint64_t job_id_source, int max_receive_attempts) {
    const auto message = make_service_method_call(target_job_name, body, job_id_source);
    return call_with_message(message, job_id_source, max_receive_attempts);
}

CmServiceMethodCallResult CmServiceMethodClient::call_with_message(
    const CmMessage& message, std::uint64_t job_id_source, int max_receive_attempts) {
    const auto send_result = connection_.send_binary(encode_cm_message(message));
    if (!send_result.ok) {
        return {false, send_result.error_message, {}, {}};
    }

    for (int attempt = 0; attempt < max_receive_attempts; ++attempt) {
        const auto received = connection_.receive();
        if (!received.ok) {
            if (debug_cm_enabled()) {
                std::cerr << "CM service receive failed: " << received.error_message << '\n';
            }
            return {false, received.error_message, {}, {}};
        }

        if (debug_cm_enabled()) {
            std::cerr << "CM service receive bytes: " << bytes_to_hex(received.bytes) << '\n';
        }

        const auto decoded = decode_cm_message(received.bytes);
        if (!decoded.has_value()) {
            return {false, "failed to decode CM message", {}, {}};
        }

        if (debug_cm_enabled()) {
            std::cerr << "CM service message: " << emsg_name(decoded->emsg) << " ("
                      << static_cast<std::uint32_t>(decoded->emsg) << ") header="
                      << bytes_to_hex(decoded->header) << " body=" << bytes_to_hex(decoded->body)
                      << '\n';
        }

        std::string unpack_error;
        const auto messages = unpack_cm_messages(*decoded, &unpack_error);
        if (messages.empty() && !unpack_error.empty()) {
            if (debug_cm_enabled()) {
                std::cerr << "CM service multi unpack failed: " << unpack_error << '\n';
            }
            return {false, unpack_error, {}, {}};
        }

        for (const auto& response_message : messages) {
            if (debug_cm_enabled() && decoded->emsg == EMsg::Multi) {
                std::cerr << "CM service multi message: " << emsg_name(response_message.emsg)
                          << " (" << static_cast<std::uint32_t>(response_message.emsg)
                          << ") header=" << bytes_to_hex(response_message.header)
                          << " body=" << bytes_to_hex(response_message.body) << '\n';
            }

            if (response_message.emsg != EMsg::ServiceMethodResponse &&
                response_message.emsg != EMsg::ServiceMethodSendToClient) {
                if (debug_cm_enabled()) {
                    std::cerr << "CM service ignored EMsg " << emsg_name(response_message.emsg)
                              << " (" << static_cast<std::uint32_t>(response_message.emsg)
                              << ")\n";
                }
                continue;
            }

            const auto header = parse_service_method_response_header(response_message.header);
            if (!header.has_value()) {
                if (debug_cm_enabled()) {
                    std::cerr << "CM service response header parse failed\n";
                }
                return {false, "failed to parse service method response header", {}, {}};
            }

            const auto matches_job = header->job_id_target == job_id_source ||
                                     (response_message.emsg == EMsg::ServiceMethodResponse &&
                                      header->job_id_target == 0);
            if (!matches_job) {
                if (debug_cm_enabled()) {
                    std::cerr << "CM service ignored response for job " << header->job_id_target
                              << ", expected " << job_id_source << '\n';
                }
                continue;
            }

            if (header->eresult != 0 && header->eresult != 1) {
                auto error = header->error_message;
                if (error.empty()) {
                    error = "service method returned eresult " + std::to_string(header->eresult);
                    const auto* name = service_method_eresult_name(header->eresult);
                    if (std::string_view{name}.size() != 0) {
                        error += " (";
                        error += name;
                        error += ")";
                    }
                }
                return {false, error, *header, response_message.body};
            }

            return {true, "", *header, response_message.body};
        }
    }

    return {false, "service method response not received", {}, {}};
}

} // namespace cauth::core::cm
