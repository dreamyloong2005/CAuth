#include "steam/cm/cm_client_hello.hpp"

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

} // namespace

CmMessage make_client_hello_message(std::uint32_t protocol_version) {
    std::vector<std::uint8_t> body;
    append_varint_field(body, 1, protocol_version);

    return CmMessage{
        EMsg::ClientHello,
        true,
        {},
        std::move(body),
    };
}

} // namespace cauth::core::cm
