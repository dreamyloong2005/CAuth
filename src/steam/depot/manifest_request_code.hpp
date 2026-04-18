#ifndef CAUTH_CORE_DEPOT_MANIFEST_REQUEST_CODE_HPP
#define CAUTH_CORE_DEPOT_MANIFEST_REQUEST_CODE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cauth::core::depot {

inline constexpr const char* kGetManifestRequestCodeMethod =
    "ContentServerDirectory.GetManifestRequestCode#1";

struct ManifestRequestCodeRequest {
    std::uint32_t app_id = 0;
    std::uint32_t depot_id = 0;
    std::uint64_t manifest_gid = 0;
    std::string branch = "public";
    std::string branch_password_hash;
};

struct ManifestRequestCodeResponse {
    std::uint64_t manifest_request_code = 0;
};

std::vector<std::uint8_t> encode_manifest_request_code_request_body(
    const ManifestRequestCodeRequest& request);
std::optional<ManifestRequestCodeResponse> parse_manifest_request_code_response_body(
    const std::vector<std::uint8_t>& bytes);

} // namespace cauth::core::depot

#endif
