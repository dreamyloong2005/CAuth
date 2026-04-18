#include "steam/depot/depot_preflight.hpp"

#include <iostream>
#include <vector>

int main() {
    cauth::core::depot::AppInfo app_info;
    app_info.app_id = 440;
    app_info.branches.push_back(cauth::core::depot::AppBranchInfo{"public", "1234"});

    cauth::core::depot::DepotBranchSelection selection;
    selection.branch = "public";
    selection.manifests.push_back(cauth::core::depot::ResolvedDepotManifest{
        441, 111, 0, 0, false, "windows", "windows", "x64", "", false});
    selection.manifests.push_back(cauth::core::depot::ResolvedDepotManifest{
        442, 222, 0, 0, false, "linux", "linux", "", "", false});

    const std::vector<cauth::core::depot::DepotDecryptionKeyResponse> keys{
        cauth::core::depot::DepotDecryptionKeyResponse{1, 441, {1, 2, 3}},
        cauth::core::depot::DepotDecryptionKeyResponse{15, 442, {}},
    };

    const auto report = cauth::core::depot::make_depot_preflight_report(
        app_info,
        selection,
        keys);
    if (!report.has_value() || report->app_id != 440 || report->branch != "public" ||
        report->build_id != "1234" || report->depots.size() != 2 ||
        report->depots[0].status != cauth::core::depot::DepotAccessStatus::Accessible ||
        !report->depots[0].key_available || report->depots[0].key_eresult != 1 ||
        report->depots[0].manifest.platform_label != "windows" ||
        report->depots[1].status != cauth::core::depot::DepotAccessStatus::Denied ||
        report->depots[1].key_available || report->depots[1].key_eresult != 15 ||
        report->depots[1].manifest.platform_label != "linux") {
        std::cerr << "depot preflight should combine manifest selection and key results\n";
        return 1;
    }

    if (std::string{cauth::core::depot::depot_access_status_name(
            cauth::core::depot::DepotAccessStatus::Accessible)} != "accessible") {
        std::cerr << "depot access status should be printable\n";
        return 1;
    }

    selection.branch = "missing";
    if (cauth::core::depot::make_depot_preflight_report(app_info, selection, keys).has_value()) {
        std::cerr << "depot preflight should reject missing branches\n";
        return 1;
    }

    return 0;
}
