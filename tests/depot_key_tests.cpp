#include "steam/cm/cm_message.hpp"
#include "steam/depot/depot_key.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const auto request = cauth::core::depot::make_depot_decryption_key_request(
        cauth::core::depot::DepotDecryptionKeyRequest{228981, 440});
    if (request.emsg != cauth::core::cm::EMsg::ClientGetDepotDecryptionKey ||
        !request.protobuf || request.body != std::vector<std::uint8_t>{0x08, 0xf5, 0xfc, 0x0d,
                                                                       0x10, 0xb8, 0x03}) {
        std::cerr << "depot key request should encode depot_id and app_id\n";
        return 1;
    }

    const auto encoded = cauth::core::cm::encode_cm_message(request);
    const auto decoded = cauth::core::cm::decode_cm_message(encoded);
    if (!decoded.has_value() || decoded->emsg != request.emsg || decoded->body != request.body) {
        std::cerr << "depot key request should round-trip through CM framing\n";
        return 1;
    }

    const std::vector<std::uint8_t> response_body{
        0x08, 0x01,
        0x10, 0xf5, 0xfc, 0x0d,
        0x1a, 0x04, 0xde, 0xad, 0xbe, 0xef,
    };
    const auto response =
        cauth::core::depot::parse_depot_decryption_key_response_body(response_body);
    if (!response.has_value() || response->eresult != 1 || response->depot_id != 228981 ||
        response->key != std::vector<std::uint8_t>{0xde, 0xad, 0xbe, 0xef}) {
        std::cerr << "depot key response should parse eresult, depot_id, and key\n";
        return 1;
    }

    if (cauth::core::depot::parse_depot_decryption_key_response_body({0x1a, 0xff})
            .has_value()) {
        std::cerr << "truncated depot key response should be rejected\n";
        return 1;
    }

    return 0;
}
