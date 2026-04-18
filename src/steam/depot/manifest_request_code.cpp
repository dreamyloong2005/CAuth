#include "steam/depot/manifest_request_code.hpp"

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

void append_string_field(std::vector<std::uint8_t>& out, int field_number,
                         const std::string& value) {
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

std::vector<std::uint8_t> encode_manifest_request_code_request_body(
    const ManifestRequestCodeRequest& request) {
    std::vector<std::uint8_t> out;
    append_varint_field(out, 1, request.app_id);
    append_varint_field(out, 2, request.depot_id);
    append_varint_field(out, 3, request.manifest_gid);
    if (!request.branch.empty()) {
        append_string_field(out, 4, request.branch);
    }
    if (!request.branch_password_hash.empty()) {
        append_string_field(out, 5, request.branch_password_hash);
    }
    return out;
}

std::optional<ManifestRequestCodeResponse> parse_manifest_request_code_response_body(
    const std::vector<std::uint8_t>& bytes) {
    ManifestRequestCodeResponse response;
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
            response.manifest_request_code = value;
            continue;
        }

        if (!skip_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return response;
}

} // namespace cauth::core::depot
