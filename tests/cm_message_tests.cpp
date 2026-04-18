#include "steam/cm/cm_message.hpp"

#include <iostream>

int main() {
    const auto raw = cauth::core::cm::raw_emsg(cauth::core::cm::EMsg::ClientLogon, true);
    if (!cauth::core::cm::is_proto_emsg(raw) ||
        cauth::core::cm::clean_emsg(raw) != cauth::core::cm::EMsg::ClientLogon) {
        std::cerr << "proto EMsg mask should round-trip\n";
        return 1;
    }

    if (std::string{cauth::core::cm::emsg_name(cauth::core::cm::EMsg::ClientLogon)} !=
        "ClientLogon") {
        std::cerr << "known EMsg should have a readable name\n";
        return 1;
    }

    if (std::string{cauth::core::cm::emsg_name(cauth::core::cm::EMsg::ClientHello)} !=
        "ClientHello") {
        std::cerr << "client hello EMsg should have a readable name\n";
        return 1;
    }

    if (std::string{cauth::core::cm::emsg_name(
            cauth::core::cm::EMsg::ClientServersAvailable)} != "ClientServersAvailable" ||
        std::string{cauth::core::cm::emsg_name(
            cauth::core::cm::EMsg::ServiceMethodCallFromClient)} !=
            "ServiceMethodCallFromClient" ||
        std::string{cauth::core::cm::emsg_name(
            cauth::core::cm::EMsg::ClientPICSProductInfoRequest)} !=
            "ClientPICSProductInfoRequest" ||
        std::string{cauth::core::cm::emsg_name(
            cauth::core::cm::EMsg::ClientGetDepotDecryptionKeyResponse)} !=
            "ClientGetDepotDecryptionKeyResponse") {
        std::cerr << "depot-related EMsg values should have readable names\n";
        return 1;
    }

    const cauth::core::cm::CmMessage message{
        cauth::core::cm::EMsg::ClientHeartBeat,
        true,
        {1, 2, 3},
        {4, 5, 6},
    };

    const auto encoded = cauth::core::cm::encode_cm_message(message);
    const auto decoded = cauth::core::cm::decode_cm_message(encoded);
    if (!decoded.has_value() || decoded->emsg != message.emsg || !decoded->protobuf ||
        decoded->header != message.header || decoded->body != message.body) {
        std::cerr << "CM proto message should encode and decode\n";
        return 1;
    }

    const cauth::core::cm::CmMessage raw_message{
        cauth::core::cm::EMsg::ClientLogOff,
        false,
        {},
        {9, 8, 7},
    };

    const auto raw_encoded = cauth::core::cm::encode_cm_message(raw_message);
    const auto raw_decoded = cauth::core::cm::decode_cm_message(raw_encoded);
    if (!raw_decoded.has_value() || raw_decoded->emsg != raw_message.emsg ||
        raw_decoded->protobuf || raw_decoded->body != raw_message.body) {
        std::cerr << "CM raw message should encode and decode\n";
        return 1;
    }

    if (cauth::core::cm::decode_cm_message({1, 2, 3}).has_value()) {
        std::cerr << "truncated CM message should be rejected\n";
        return 1;
    }

    if (cauth::core::cm::bytes_to_hex({0x0a, 0xff}) != "0aff") {
        std::cerr << "hex helper should encode bytes predictably\n";
        return 1;
    }

    const cauth::core::cm::CmMessage child{
        cauth::core::cm::EMsg::ClientLogOnResponse,
        true,
        {},
        {0x08, 0x01},
    };
    const auto child_encoded = cauth::core::cm::encode_cm_message(child);

    std::vector<std::uint8_t> multi_payload;
    const auto child_size = static_cast<std::uint32_t>(child_encoded.size());
    for (int shift = 0; shift < 32; shift += 8) {
        multi_payload.push_back(static_cast<std::uint8_t>((child_size >> shift) & 0xffU));
    }
    multi_payload.insert(multi_payload.end(), child_encoded.begin(), child_encoded.end());

    std::vector<std::uint8_t> multi_body;
    multi_body.push_back(0x12);
    multi_body.push_back(static_cast<std::uint8_t>(multi_payload.size()));
    multi_body.insert(multi_body.end(), multi_payload.begin(), multi_payload.end());

    const cauth::core::cm::CmMessage multi{
        cauth::core::cm::EMsg::Multi,
        true,
        {},
        multi_body,
    };

    std::string unpack_error;
    const auto unpacked = cauth::core::cm::unpack_cm_messages(multi, &unpack_error);
    if (unpacked.size() != 1 || !unpack_error.empty() ||
        unpacked[0].emsg != cauth::core::cm::EMsg::ClientLogOnResponse ||
        unpacked[0].body != child.body) {
        std::cerr << "CMsgMulti should unpack length-prefixed child messages\n";
        return 1;
    }

    return 0;
}
