#pragma once

#include "core/session/session_repository.hpp"
#include "steam/cloud/steam_cloud_types.hpp"

#include <iosfwd>

namespace cauth::steam::cloud {

enum class SteamCloudTransferKind {
    Pull = 1,
    Push = 2,
    Verify = 3,
};

enum class SteamCloudRouteTask {
    List = 1,
    Verify = 2,
    Pull = 3,
    Push = 4,
};

struct SteamCloudTransferProgress {
    SteamCloudTransferKind kind = SteamCloudTransferKind::Pull;
    std::string phase;
    std::string target;
    std::uint64_t completed_steps = 0;
    std::uint64_t total_steps = 0;
    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
    std::string module_status = "idle";
    bool resumable = false;
    bool resumed = false;
    std::uint64_t resume_from_bytes = 0;
};

using SteamCloudTransferProgressHook =
    void (*)(const SteamCloudTransferProgress& progress, void* user_data);
using SteamCloudTransferCancelHook = bool (*)(void* user_data);
using SteamCloudTransferPauseHook = bool (*)(void* user_data);

struct SteamCloudRouteEntry {
    std::string endpoint;
    std::string protocol;
    std::string role;
    std::string note;
    bool latency_known = false;
    std::uint64_t latency_ms = 0;
    bool recent_success = false;
    bool recent_failure = false;
    std::uint32_t success_count = 0;
    std::uint32_t failure_count = 0;
};

struct SteamCloudRouteReport {
    bool ok = false;
    std::string module_status = "idle";
    std::string backend;
    std::string message;
    std::vector<SteamCloudRouteEntry> routes;
};

void set_current_thread_steam_cloud_transfer_hooks(
    SteamCloudTransferProgressHook progress_hook,
    SteamCloudTransferCancelHook cancel_hook,
    SteamCloudTransferPauseHook pause_hook,
    void* user_data);
void clear_current_thread_steam_cloud_transfer_hooks();

SteamCloudRouteReport probe_cloud_routes(core::session::SessionRepository& store,
                                         const SteamCloudRequest& request,
                                         SteamCloudRouteTask task,
                                         std::uint32_t max_count = 5);
int print_cloud_routes(core::session::SessionRepository& store,
                       const SteamCloudRequest& request,
                       SteamCloudRouteTask task,
                       std::uint32_t max_count,
                       std::ostream& out,
                       std::ostream& err);

SteamCloudFileListResult list_remote_files(const SteamCloudRequest& request,
                                           std::uint32_t count = 500,
                                           std::uint32_t start_index = 0,
                                           bool extended_details = true);
SteamCloudFileListResult list_remote_files_via_web_page_diagnostic(
    core::session::SessionRepository& store,
    const SteamCloudRequest& request,
    std::uint32_t count = 500,
    std::uint32_t start_index = 0);
SteamCloudVerifyResult verify_cloud_local_files(const SteamCloudRequest& request,
                                                bool include_extra_local = false,
                                                std::uint32_t page_size = 500,
                                                bool extended_details = true);
SteamCloudResult pull_cloud_save(const SteamCloudRequest& request);
SteamCloudResult push_cloud_save(const SteamCloudRequest& request);
int print_remote_files(core::session::SessionRepository& store,
                       const SteamCloudRequest& request,
                       std::uint32_t count,
                       std::uint32_t start_index,
                       bool extended_details,
                       std::ostream& out,
                       std::ostream& err);
int print_remote_files_via_web_page_diagnostic(core::session::SessionRepository& store,
                                               const SteamCloudRequest& request,
                                               std::uint32_t count,
                                               std::uint32_t start_index,
                                               std::ostream& out,
                                               std::ostream& err);
int run_pull_cloud_save(core::session::SessionRepository& store,
                        const SteamCloudRequest& request,
                        std::ostream& out,
                        std::ostream& err);
int run_push_cloud_save(core::session::SessionRepository& store,
                        const SteamCloudRequest& request,
                        std::ostream& out,
                        std::ostream& err);
int run_verify_cloud_local(core::session::SessionRepository& store,
                           const SteamCloudRequest& request,
                           bool include_extra_local,
                           std::ostream& out,
                           std::ostream& err);

} // namespace cauth::steam::cloud
