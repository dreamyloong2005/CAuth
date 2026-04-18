#ifndef CAUTH_CORE_DEPOT_DEPOT_RESOLVER_HPP
#define CAUTH_CORE_DEPOT_DEPOT_RESOLVER_HPP

#include "steam/depot/app_info.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::core::depot {

struct ResolvedDepotManifest {
    std::uint32_t depot_id = 0;
    std::uint64_t manifest_gid = 0;
    std::uint64_t size = 0;
    std::uint64_t download_size = 0;
    bool encrypted = false;
    std::string platform_label;
    std::string os_list;
    std::string os_arch;
    std::string depot_from_app;
    bool shared_install = false;
};

struct DepotBranchSelection {
    std::string branch;
    std::vector<ResolvedDepotManifest> manifests;
};

std::vector<AppBranchInfo> available_branches(const AppInfo& app_info);
std::optional<AppBranchInfo> find_branch(const AppInfo& app_info, std::string_view branch);
std::optional<DepotBranchSelection> resolve_depot_manifests(const AppInfo& app_info,
                                                            std::string_view branch);

} // namespace cauth::core::depot

#endif
