#pragma once

#include "core/platform/http_client.hpp"
#include "core/platform/route_selection.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cauth::steam::cloud {

struct SteamCloudUploadFile {
    std::string local_path;
    std::string filename;
    std::uint32_t file_size = 0;
    std::string file_sha;
    std::vector<std::string> platforms_to_sync;
    std::vector<std::uint8_t> bytes;
};

struct SteamCloudUploadResult {
    bool ok = false;
    std::string message;
    bool resumable = false;
    bool resumed = false;
    std::uint64_t resume_from_bytes = 0;
};

struct SteamCloudWebAuthContext {
    std::string access_token;
    std::string web_cookie_header;
    std::string store_cookie_header;
    cauth::core::platform::RouteSelection route_selection;
};

using SteamCloudUploadProgressHook =
    void (*)(std::string_view filename,
             std::uint64_t bytes_transferred,
             std::uint64_t total_bytes,
             void* user_data);
using SteamCloudUploadStateHook =
    void (*)(bool resumable, bool resumed, std::uint64_t resume_from_bytes, void* user_data);
using SteamCloudUploadCancelHook = bool (*)(void* user_data);
using SteamCloudUploadPauseHook = bool (*)(void* user_data);

struct SteamCloudUploadCallbacks {
    SteamCloudUploadProgressHook progress_hook = nullptr;
    SteamCloudUploadStateHook state_hook = nullptr;
    SteamCloudUploadCancelHook cancel_hook = nullptr;
    SteamCloudUploadPauseHook pause_hook = nullptr;
    void* user_data = nullptr;
};

std::string build_begin_app_upload_batch_form_body(
    std::string_view access_token,
    std::uint32_t app_id,
    std::string_view machine_name,
    const std::vector<std::string>& files_to_upload,
    const std::vector<std::string>& files_to_delete);
std::string build_begin_app_upload_batch_form_body(
    const SteamCloudWebAuthContext& auth,
    std::uint32_t app_id,
    std::string_view machine_name,
    const std::vector<std::string>& files_to_upload,
    const std::vector<std::string>& files_to_delete);

SteamCloudUploadResult upload_cloud_files(std::string_view access_token,
                                          std::uint32_t app_id,
                                          std::string_view machine_name,
                                          const std::vector<SteamCloudUploadFile>& files,
                                          const std::vector<std::string>& files_to_delete,
                                          const SteamCloudUploadCallbacks& callbacks = {});
SteamCloudUploadResult upload_cloud_files(const SteamCloudWebAuthContext& auth,
                                          std::uint32_t app_id,
                                          std::string_view machine_name,
                                          const std::vector<SteamCloudUploadFile>& files,
                                          const std::vector<std::string>& files_to_delete,
                                          const SteamCloudUploadCallbacks& callbacks = {});

} // namespace cauth::steam::cloud
