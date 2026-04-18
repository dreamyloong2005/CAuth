#ifndef CAUTH_CORE_DEPOT_PICS_HPP
#define CAUTH_CORE_DEPOT_PICS_HPP

#include "steam/cm/cm_message.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace cauth::core::depot {

struct PicsProductInfoAppRequest {
    std::uint32_t app_id = 0;
    std::uint64_t access_token = 0;
    bool only_public = false;
};

struct PicsProductInfoRequest {
    std::vector<PicsProductInfoAppRequest> apps;
    bool meta_data_only = false;
    bool supports_package_tokens = true;
};

struct PicsProductInfoApp {
    std::uint32_t app_id = 0;
    std::uint32_t change_number = 0;
    bool missing_token = false;
    std::vector<std::uint8_t> sha;
    std::vector<std::uint8_t> buffer;
    bool only_public = false;
    std::uint32_t size = 0;
};

struct PicsProductInfoResponse {
    std::uint32_t apps_unknown = 0;
    std::uint32_t packages_unknown = 0;
    bool response_pending = false;
    std::vector<PicsProductInfoApp> apps;
};

std::vector<std::uint8_t> encode_pics_product_info_request_body(
    const PicsProductInfoRequest& request);
cm::CmMessage make_pics_product_info_request(const PicsProductInfoRequest& request);
std::optional<PicsProductInfoResponse> parse_pics_product_info_response_body(
    const std::vector<std::uint8_t>& bytes);

} // namespace cauth::core::depot

#endif
