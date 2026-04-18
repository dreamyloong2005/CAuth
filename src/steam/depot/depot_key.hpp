#ifndef CAUTH_CORE_DEPOT_DEPOT_KEY_HPP
#define CAUTH_CORE_DEPOT_DEPOT_KEY_HPP

#include "steam/cm/cm_message.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace cauth::core::depot {

struct DepotDecryptionKeyRequest {
    std::uint32_t depot_id = 0;
    std::uint32_t app_id = 0;
};

struct DepotDecryptionKeyResponse {
    std::int32_t eresult = 0;
    std::uint32_t depot_id = 0;
    std::vector<std::uint8_t> key;
};

std::vector<std::uint8_t> encode_depot_decryption_key_request_body(
    const DepotDecryptionKeyRequest& request);
cm::CmMessage make_depot_decryption_key_request(const DepotDecryptionKeyRequest& request);
std::optional<DepotDecryptionKeyResponse> parse_depot_decryption_key_response_body(
    const std::vector<std::uint8_t>& bytes);

} // namespace cauth::core::depot

#endif
