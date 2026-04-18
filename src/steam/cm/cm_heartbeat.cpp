#include "steam/cm/cm_heartbeat.hpp"
#include "steam/auth/steam_session_identity.hpp"

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

} // namespace

CmMessage make_client_heartbeat_message(const session::AuthSession& session) {
    std::vector<std::uint8_t> header;
    append_fixed64_field(header, 1, cauth::steam::auth::steam_id(session));

    return CmMessage{
        EMsg::ClientHeartBeat,
        true,
        std::move(header),
        {},
    };
}

} // namespace cauth::core::cm
