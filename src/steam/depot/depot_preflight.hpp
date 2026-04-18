#ifndef CAUTH_CORE_DEPOT_DEPOT_PREFLIGHT_HPP
#define CAUTH_CORE_DEPOT_DEPOT_PREFLIGHT_HPP

#include "steam/depot/app_info.hpp"
#include "steam/depot/depot_key.hpp"
#include "steam/depot/depot_resolver.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cauth::core::depot {

enum class DepotAccessStatus {
    Unknown,
    Accessible,
    Denied,
};

struct DepotAccessCheck {
    ResolvedDepotManifest manifest;
    DepotAccessStatus status = DepotAccessStatus::Unknown;
    std::int32_t key_eresult = 0;
    bool key_available = false;
};

struct DepotPreflightReport {
    std::uint32_t app_id = 0;
    std::string branch;
    std::string build_id;
    std::vector<DepotAccessCheck> depots;
};

DepotAccessStatus classify_depot_key_access(const DepotDecryptionKeyResponse& response);
const char* depot_access_status_name(DepotAccessStatus status);
std::optional<DepotPreflightReport> make_depot_preflight_report(
    const AppInfo& app_info,
    const DepotBranchSelection& selection,
    const std::vector<DepotDecryptionKeyResponse>& key_responses);

} // namespace cauth::core::depot

#endif
