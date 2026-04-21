#pragma once

#include "core/platform/http_client.hpp"
#include "steam/cloud/steam_cloud_upload_service.hpp"
#include "steam/cloud/steam_cloud_types.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace cauth::steam::cloud::testing {

using ListRemoteFilesHook = SteamCloudFileListResult (*)(
    const SteamCloudRequest& request,
    std::uint32_t count,
    std::uint32_t start_index,
    bool extended_details);

using DownloadFileHook = SteamCloudDownloadResult (*)(
    const SteamCloudRequest& request,
    const SteamCloudFileEntry& file);

using UploadCloudFilesHook = SteamCloudUploadResult (*)(
    const SteamCloudWebAuthContext& auth,
    std::uint32_t app_id,
    std::string_view machine_name,
    const std::vector<SteamCloudUploadFile>& files,
    const std::vector<std::string>& files_to_delete);

void set_list_remote_files_hook(ListRemoteFilesHook hook);
void set_download_file_hook(DownloadFileHook hook);
void set_upload_cloud_files_hook(UploadCloudFilesHook hook);
void clear_cloud_test_hooks();

} // namespace cauth::steam::cloud::testing
