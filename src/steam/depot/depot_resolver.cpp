#include "steam/depot/depot_resolver.hpp"

namespace cauth::core::depot {

std::vector<AppBranchInfo> available_branches(const AppInfo& app_info) {
    return app_info.branches;
}

std::optional<AppBranchInfo> find_branch(const AppInfo& app_info, std::string_view branch) {
    for (const auto& candidate : app_info.branches) {
        if (candidate.name == branch) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<DepotBranchSelection> resolve_depot_manifests(const AppInfo& app_info,
                                                            std::string_view branch) {
    if (!find_branch(app_info, branch).has_value()) {
        return std::nullopt;
    }

    DepotBranchSelection selection;
    selection.branch = std::string{branch};
    for (const auto& depot : app_info.depots) {
        for (const auto& manifest : depot.manifests) {
            if (manifest.branch != branch) {
                continue;
            }
            if (manifest.manifest_gid == 0) {
                continue;
            }

            selection.manifests.push_back(ResolvedDepotManifest{
                depot.depot_id,
                manifest.manifest_gid,
                manifest.size,
                manifest.download_size,
                manifest.encrypted,
                depot_platform_label(depot.os_list, depot.os_arch),
                depot.os_list,
                depot.os_arch,
                depot.depot_from_app,
                depot.shared_install,
            });
            break;
        }
    }

    return selection;
}

} // namespace cauth::core::depot
