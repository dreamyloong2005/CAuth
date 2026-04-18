#include "steam/cm/cm_message.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>

#ifdef CAUTH_HAS_ZLIB
#include <zlib.h>
#endif

namespace cauth::core::cm {
namespace {

void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
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

bool read_length_delimited(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                           std::vector<std::uint8_t>& value) {
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
    std::vector<std::uint8_t> ignored_bytes;
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
        return read_length_delimited(bytes, offset, ignored_bytes);
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

struct MultiBody {
    std::uint32_t size_unzipped = 0;
    std::vector<std::uint8_t> message_body;
};

std::optional<MultiBody> parse_multi_body(const std::vector<std::uint8_t>& bytes) {
    MultiBody multi;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(bytes, offset, tag)) {
            return std::nullopt;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);

        if (field_number == 1 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            multi.size_unzipped = static_cast<std::uint32_t>(value);
            continue;
        }

        if (field_number == 2 && wire_type == 2) {
            if (!read_length_delimited(bytes, offset, multi.message_body)) {
                return std::nullopt;
            }
            continue;
        }

        if (!skip_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return multi;
}

bool inflate_multi_body(const std::vector<std::uint8_t>& compressed,
                        std::uint32_t size_unzipped,
                        std::vector<std::uint8_t>& decompressed,
                        std::string* error_message) {
#ifdef CAUTH_HAS_ZLIB
    decompressed.assign(size_unzipped, 0);

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef*>(decompressed.data());
    stream.avail_out = static_cast<uInt>(decompressed.size());

    auto result = inflateInit2(&stream, 16 + MAX_WBITS);
    if (result != Z_OK) {
        if (error_message != nullptr) {
            *error_message = "zlib inflateInit2 failed";
        }
        return false;
    }

    result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (result != Z_STREAM_END || stream.total_out != size_unzipped) {
        if (error_message != nullptr) {
            *error_message = "zlib inflate failed";
        }
        return false;
    }

    return true;
#else
    (void)compressed;
    (void)size_unzipped;
    (void)decompressed;
    if (error_message != nullptr) {
        *error_message = "compressed CMsgMulti requires zlib support";
    }
    return false;
#endif
}

} // namespace

std::uint32_t raw_emsg(EMsg emsg, bool protobuf) {
    auto raw = static_cast<std::uint32_t>(emsg);
    if (protobuf) {
        raw |= kProtoMask;
    }
    return raw;
}

EMsg clean_emsg(std::uint32_t raw) {
    return static_cast<EMsg>(raw & ~kProtoMask);
}

bool is_proto_emsg(std::uint32_t raw) {
    return (raw & kProtoMask) != 0;
}

const char* emsg_name(EMsg emsg) {
    switch (emsg) {
    case EMsg::Invalid:
        return "Invalid";
    case EMsg::Multi:
        return "Multi";
    case EMsg::ServiceMethodResponse:
        return "ServiceMethodResponse";
    case EMsg::ServiceMethodCallFromClient:
        return "ServiceMethodCallFromClient";
    case EMsg::ServiceMethodSendToClient:
        return "ServiceMethodSendToClient";
    case EMsg::ClientLogOnResponse:
        return "ClientLogOnResponse";
    case EMsg::ClientLogOff:
        return "ClientLogOff";
    case EMsg::ClientHeartBeat:
        return "ClientHeartBeat";
    case EMsg::ClientSessionToken:
        return "ClientSessionToken";
    case EMsg::ClientGetDepotDecryptionKey:
        return "ClientGetDepotDecryptionKey";
    case EMsg::ClientGetDepotDecryptionKeyResponse:
        return "ClientGetDepotDecryptionKeyResponse";
    case EMsg::ClientServersAvailable:
        return "ClientServersAvailable";
    case EMsg::ClientLogon:
        return "ClientLogon";
    case EMsg::ClientPICSProductInfoRequest:
        return "ClientPICSProductInfoRequest";
    case EMsg::ClientPICSProductInfoResponse:
        return "ClientPICSProductInfoResponse";
    case EMsg::ClientPICSAccessTokenRequest:
        return "ClientPICSAccessTokenRequest";
    case EMsg::ClientPICSAccessTokenResponse:
        return "ClientPICSAccessTokenResponse";
    case EMsg::ServiceMethodCallFromClientNonAuthed:
        return "ServiceMethodCallFromClientNonAuthed";
    case EMsg::ClientHello:
        return "ClientHello";
    default:
        return "Unknown";
    }
}

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

std::vector<std::uint8_t> encode_cm_message(const CmMessage& message) {
    std::vector<std::uint8_t> out;
    append_u32_le(out, raw_emsg(message.emsg, message.protobuf));

    if (message.protobuf) {
        append_u32_le(out, static_cast<std::uint32_t>(message.header.size()));
        out.insert(out.end(), message.header.begin(), message.header.end());
    }

    out.insert(out.end(), message.body.begin(), message.body.end());
    return out;
}

std::optional<CmMessage> decode_cm_message(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < sizeof(std::uint32_t)) {
        return std::nullopt;
    }

    const auto raw = read_u32_le(bytes, 0);
    CmMessage message;
    message.emsg = clean_emsg(raw);
    message.protobuf = is_proto_emsg(raw);

    std::size_t offset = sizeof(std::uint32_t);
    if (message.protobuf) {
        if (bytes.size() - offset < sizeof(std::uint32_t)) {
            return std::nullopt;
        }

        const auto header_size = read_u32_le(bytes, offset);
        offset += sizeof(std::uint32_t);
        if (bytes.size() - offset < header_size) {
            return std::nullopt;
        }

        message.header.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                              bytes.begin() + static_cast<std::ptrdiff_t>(offset + header_size));
        offset += header_size;
    }

    message.body.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    return message;
}

std::vector<CmMessage> unpack_cm_messages(const CmMessage& message, std::string* error_message) {
    if (error_message != nullptr) {
        error_message->clear();
    }

    if (message.emsg != EMsg::Multi) {
        return {message};
    }

    const auto multi = parse_multi_body(message.body);
    if (!multi.has_value()) {
        if (error_message != nullptr) {
            *error_message = "failed to parse CMsgMulti body";
        }
        return {};
    }

    std::vector<std::uint8_t> decompressed;
    const auto* payload = &multi->message_body;
    if (multi->size_unzipped != 0) {
        if (!inflate_multi_body(multi->message_body, multi->size_unzipped, decompressed,
                                error_message)) {
            return {};
        }
        payload = &decompressed;
    }

    std::vector<CmMessage> messages;
    std::size_t offset = 0;
    while (offset < payload->size()) {
        if (payload->size() - offset < sizeof(std::uint32_t)) {
            if (error_message != nullptr) {
                *error_message = "truncated CMsgMulti message length";
            }
            return {};
        }

        const auto message_size = read_u32_le(*payload, offset);
        offset += sizeof(std::uint32_t);
        if (payload->size() - offset < message_size) {
            if (error_message != nullptr) {
                *error_message = "truncated CMsgMulti message payload";
            }
            return {};
        }

        std::vector<std::uint8_t> child_bytes{
            payload->begin() + static_cast<std::ptrdiff_t>(offset),
            payload->begin() + static_cast<std::ptrdiff_t>(offset + message_size),
        };
        offset += message_size;

        const auto child = decode_cm_message(child_bytes);
        if (!child.has_value()) {
            if (error_message != nullptr) {
                *error_message = "failed to decode CMsgMulti child message";
            }
            return {};
        }

        messages.push_back(*child);
    }

    return messages;
}

} // namespace cauth::core::cm
