#include "steam/depot/pics.hpp"

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

void append_bytes_field(std::vector<std::uint8_t>& out, int field_number,
                        const std::vector<std::uint8_t>& value) {
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

std::vector<std::uint8_t> encode_app_request(const PicsProductInfoAppRequest& app) {
    std::vector<std::uint8_t> out;
    append_varint_field(out, 1, app.app_id);
    if (app.access_token != 0) {
        append_varint_field(out, 2, app.access_token);
    }
    if (app.only_public) {
        append_varint_field(out, 3, 1);
    }
    return out;
}

std::optional<PicsProductInfoApp> parse_app_info(const std::vector<std::uint8_t>& bytes) {
    PicsProductInfoApp app;
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
            app.app_id = static_cast<std::uint32_t>(value);
            continue;
        }

        if (field_number == 2 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            app.change_number = static_cast<std::uint32_t>(value);
            continue;
        }

        if (field_number == 3 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            app.missing_token = value != 0;
            continue;
        }

        if (field_number == 4 && wire_type == 2) {
            if (!read_length_delimited(bytes, offset, app.sha)) {
                return std::nullopt;
            }
            continue;
        }

        if (field_number == 5 && wire_type == 2) {
            if (!read_length_delimited(bytes, offset, app.buffer)) {
                return std::nullopt;
            }
            continue;
        }

        if (field_number == 6 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            app.only_public = value != 0;
            continue;
        }

        if (field_number == 7 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            app.size = static_cast<std::uint32_t>(value);
            continue;
        }

        if (!skip_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return app;
}

} // namespace

std::vector<std::uint8_t> encode_pics_product_info_request_body(
    const PicsProductInfoRequest& request) {
    std::vector<std::uint8_t> out;
    for (const auto& app : request.apps) {
        append_bytes_field(out, 2, encode_app_request(app));
    }
    if (request.meta_data_only) {
        append_varint_field(out, 3, 1);
    }
    if (request.supports_package_tokens) {
        append_varint_field(out, 5, 1);
    }
    return out;
}

cm::CmMessage make_pics_product_info_request(const PicsProductInfoRequest& request) {
    return cm::CmMessage{
        cm::EMsg::ClientPICSProductInfoRequest,
        true,
        {},
        encode_pics_product_info_request_body(request),
    };
}

std::optional<PicsProductInfoResponse> parse_pics_product_info_response_body(
    const std::vector<std::uint8_t>& bytes) {
    PicsProductInfoResponse response;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(bytes, offset, tag)) {
            return std::nullopt;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);
        if ((field_number == 2 || field_number == 4 || field_number == 6) && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            if (field_number == 2) {
                ++response.apps_unknown;
            } else if (field_number == 4) {
                ++response.packages_unknown;
            } else {
                response.response_pending = value != 0;
            }
            continue;
        }

        if (field_number == 1 && wire_type == 2) {
            std::vector<std::uint8_t> app_bytes;
            if (!read_length_delimited(bytes, offset, app_bytes)) {
                return std::nullopt;
            }
            const auto app = parse_app_info(app_bytes);
            if (!app.has_value()) {
                return std::nullopt;
            }
            response.apps.push_back(*app);
            continue;
        }

        if (!skip_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return response;
}

} // namespace cauth::core::depot
