#pragma once

#include "core/platform/file_write.hpp"
#include "core/platform/route_selection.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cauth::steam::cloud {

enum class SteamCloudDirection {
    Pull,
    Push,
};

enum class SteamCloudConflictPolicy {
    Default,
    LocalWins,
    RemoteWins,
    NewerWins,
    FailOnConflict,
};

enum class SteamCloudBackend {
    Auto,
    WebApi,
    CmCloud,
};

struct SteamCloudRequest {
    std::uint32_t app_id = 0;
    std::string access_token;
    std::string web_cookie_header;
    std::string store_cookie_header;
    std::string refresh_token;
    std::string session_type;
    std::uint64_t steam_id = 0;
    std::string local_root;
    std::string remote_root;
    bool dry_run = false;
    bool delete_remote_orphans = false;
    SteamCloudConflictPolicy conflict_policy = SteamCloudConflictPolicy::Default;
    SteamCloudBackend backend = SteamCloudBackend::Auto;
    cauth::core::platform::FileWriteOptions local_write_options{};
    cauth::core::platform::RouteSelection route_selection;
};

struct SteamCloudFileEntry {
    std::uint32_t app_id = 0;
    std::uint64_t ugc_id = 0;
    std::string filename;
    std::uint64_t timestamp = 0;
    std::uint32_t file_size = 0;
    std::string url;
    std::uint64_t steam_id_creator = 0;
    std::uint32_t flags = 0;
    std::string platforms_to_sync;
    std::string file_sha;
};

struct SteamCloudFileListResult {
    bool ok = false;
    std::uint32_t app_id = 0;
    std::uint32_t total_files = 0;
    std::uint32_t eresult = 0;
    std::string module_status = "idle";
    std::vector<SteamCloudFileEntry> files;
    std::string message;
};

struct SteamCloudDownloadResult {
    bool ok = false;
    std::vector<std::uint8_t> bytes;
    std::uint32_t file_size = 0;
    std::uint32_t raw_file_size = 0;
    bool encrypted = false;
    std::string message;
};

struct SteamCloudResult {
    bool ok = false;
    std::uint32_t app_id = 0;
    SteamCloudDirection direction = SteamCloudDirection::Pull;
    SteamCloudConflictPolicy conflict_policy = SteamCloudConflictPolicy::Default;
    std::string module_status = "idle";
    std::uint64_t local_file_count = 0;
    std::uint64_t remote_file_count = 0;
    std::uint64_t transferred_count = 0;
    std::uint64_t deleted_count = 0;
    std::uint64_t skipped_count = 0;
    std::uint64_t conflict_count = 0;
    std::uint64_t transferred_bytes = 0;
    bool resumable = false;
    bool resumed = false;
    std::uint64_t resume_from_bytes = 0;
    std::string message;
};

enum class SteamCloudVerifyStatus {
    Ok,
    MissingLocal,
    Mismatched,
    SizeOnly,
    ExtraLocal,
};

struct SteamCloudVerifyEntry {
    std::string remote_filename;
    std::string local_path;
    SteamCloudVerifyStatus status = SteamCloudVerifyStatus::Ok;
    std::uint32_t remote_size = 0;
    std::uint64_t remote_timestamp = 0;
    std::string remote_sha;
    std::uint64_t local_size = 0;
    std::string local_sha;
    std::string reason;
};

struct SteamCloudVerifyResult {
    bool ok = false;
    bool fatal_error = false;
    bool include_extra_local = false;
    std::uint32_t app_id = 0;
    std::string module_status = "idle";
    std::uint64_t checked_count = 0;
    std::uint64_t ok_count = 0;
    std::uint64_t missing_count = 0;
    std::uint64_t mismatched_count = 0;
    std::uint64_t size_only_count = 0;
    std::uint64_t filtered_out_count = 0;
    std::uint64_t extra_local_count = 0;
    std::uint64_t total_count = 0;
    std::vector<SteamCloudVerifyEntry> entries;
    std::string message;

    [[nodiscard]] bool clean() const {
        return !fatal_error && missing_count == 0 && mismatched_count == 0;
    }
};

} // namespace cauth::steam::cloud
