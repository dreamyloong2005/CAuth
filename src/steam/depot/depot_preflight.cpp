#include "steam/depot/depot_preflight.hpp"

namespace cauth::core::depot {

DepotAccessStatus classify_depot_key_access(const DepotDecryptionKeyResponse& response) {
    if (response.eresult == 1 && !response.key.empty()) {
        return DepotAccessStatus::Accessible;
    }
    return DepotAccessStatus::Denied;
}

const char* depot_access_status_name(DepotAccessStatus status) {
    switch (status) {
    case DepotAccessStatus::Unknown:
        return "unknown";
    case DepotAccessStatus::Accessible:
        return "accessible";
    case DepotAccessStatus::Denied:
        return "denied";
    }
    return "unknown";
}

std::optional<DepotPreflightReport> make_depot_preflight_report(
    const AppInfo& app_info,
    const DepotBranchSelection& selection,
    const std::vector<DepotDecryptionKeyResponse>& key_responses) {
    const auto branch = find_branch(app_info, selection.branch);
    if (!branch.has_value()) {
        return std::nullopt;
    }

    DepotPreflightReport report;
    report.app_id = app_info.app_id;
    report.branch = selection.branch;
    report.build_id = branch->build_id;

    for (const auto& manifest : selection.manifests) {
        DepotAccessCheck check;
        check.manifest = manifest;
        for (const auto& response : key_responses) {
            if (response.depot_id != 0 && response.depot_id != manifest.depot_id) {
                continue;
            }
            check.key_eresult = response.eresult;
            check.key_available = !response.key.empty();
            check.status = classify_depot_key_access(response);
            break;
        }
        report.depots.push_back(std::move(check));
    }

    return report;
}

} // namespace cauth::core::depot
