#ifndef CAUTH_CORE_DEPOT_APP_INFO_HPP
#define CAUTH_CORE_DEPOT_APP_INFO_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cauth::core::depot {

struct DepotManifestInfo {
    std::string branch;
    std::uint64_t manifest_gid = 0;
    std::uint64_t size = 0;
    std::uint64_t download_size = 0;
    bool encrypted = false;
};

struct DepotInfo {
    std::uint32_t depot_id = 0;
    std::string os_list;
    std::string os_arch;
    std::string languages;
    std::string depot_from_app;
    bool shared_install = false;
    std::vector<DepotManifestInfo> manifests;
};

struct AppBranchInfo {
    std::string name;
    std::string build_id;
    std::string description;
    std::uint32_t time_updated = 0;
    bool password_required = false;
};

struct AppInfo {
    std::uint32_t app_id = 0;
    std::vector<AppBranchInfo> branches;
    std::vector<DepotInfo> depots;
};

struct AppInfoParseDebug {
    bool ok = false;
    std::size_t buffer_size = 0;
    std::size_t best_offset = 0;
    std::uint8_t best_end_marker = 0;
    std::size_t best_nodes = 0;
    std::size_t best_score = 0;
    std::string prefix_hex;
};

std::optional<AppInfo> parse_app_info_buffer(const std::vector<std::uint8_t>& bytes);
std::optional<AppInfo> parse_app_info_buffer(const std::vector<std::uint8_t>& bytes,
                                             AppInfoParseDebug* debug);
std::vector<std::string> depot_platform_tags(std::string_view os_list);
std::string depot_platform_label(std::string_view os_list, std::string_view os_arch = {});

} // namespace cauth::core::depot

#endif
