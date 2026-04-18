#include "steam/cm/cm_client_hello.hpp"
#include "steam/cm/cm_message.hpp"

#include <iostream>

int main() {
    const auto hello = cauth::core::cm::make_client_hello_message();
    if (hello.emsg != cauth::core::cm::EMsg::ClientHello || !hello.protobuf ||
        hello.body != std::vector<std::uint8_t>{0x08, 0xac, 0x80, 0x04}) {
        std::cerr << "ClientHello should encode protocol_version 65580\n";
        return 1;
    }

    const auto encoded = cauth::core::cm::encode_cm_message(hello);
    const auto decoded = cauth::core::cm::decode_cm_message(encoded);
    if (!decoded.has_value() || decoded->emsg != cauth::core::cm::EMsg::ClientHello ||
        decoded->body != hello.body) {
        std::cerr << "ClientHello should round-trip through CM framing\n";
        return 1;
    }

    return 0;
}
