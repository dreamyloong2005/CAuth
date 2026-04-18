#include "steam/depot/depot_key.hpp"

namespace cauth::core::depot {
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

} // namespace

std::vector<std::uint8_t> encode_depot_decryption_key_request_body(
    const DepotDecryptionKeyRequest& request) {
    std::vector<std::uint8_t> out;
    append_varint_field(out, 1, request.depot_id);
    append_varint_field(out, 2, request.app_id);
    return out;
}

cm::CmMessage make_depot_decryption_key_request(const DepotDecryptionKeyRequest& request) {
    return cm::CmMessage{
        cm::EMsg::ClientGetDepotDecryptionKey,
        true,
        {},
        encode_depot_decryption_key_request_body(request),
    };
}

std::optional<DepotDecryptionKeyResponse> parse_depot_decryption_key_response_body(
    const std::vector<std::uint8_t>& bytes) {
    DepotDecryptionKeyResponse response;
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
            response.eresult = static_cast<std::int32_t>(value);
            continue;
        }

        if (field_number == 2 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            response.depot_id = static_cast<std::uint32_t>(value);
            continue;
        }

        if (field_number == 3 && wire_type == 2) {
            if (!read_length_delimited(bytes, offset, response.key)) {
                return std::nullopt;
            }
            continue;
        }

        if (!skip_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return response;
}

} // namespace cauth::core::depot
