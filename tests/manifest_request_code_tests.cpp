#include "steam/depot/manifest_request_code.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const auto body = cauth::core::depot::encode_manifest_request_code_request_body(
        cauth::core::depot::ManifestRequestCodeRequest{
            440,
            441,
            257913086909807568ULL,
            "public",
            "dead",
        });

    const std::vector<std::uint8_t> expected{
        0x08, 0xb8, 0x03,
        0x10, 0xb9, 0x03,
        0x18, 0xd0, 0xf7, 0xbb, 0xc1, 0xe0, 0xd2, 0x92, 0xca, 0x03,
        0x22, 0x06, 'p', 'u', 'b', 'l', 'i', 'c',
        0x2a, 0x04, 'd', 'e', 'a', 'd',
    };
    if (body != expected) {
        std::cerr << "manifest request-code request should encode app, depot, gid, branch, and "
                     "branch password hash\n";
        return 1;
    }

    const std::vector<std::uint8_t> response_body{
        0x08, 0x88, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
    };
    const auto response =
        cauth::core::depot::parse_manifest_request_code_response_body(response_body);
    if (!response.has_value() || response->manifest_request_code != 9223372036854775560ULL) {
        std::cerr << "manifest request-code response should parse the request code\n";
        return 1;
    }

    if (cauth::core::depot::parse_manifest_request_code_response_body({0x08, 0xff})
            .has_value()) {
        std::cerr << "truncated manifest request-code response should be rejected\n";
        return 1;
    }

    return 0;
}
