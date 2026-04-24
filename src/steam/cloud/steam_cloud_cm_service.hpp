#pragma once

#include "steam/cloud/steam_cloud_service.hpp"
#include "steam/cloud/steam_cloud_upload_service.hpp"
#include "steam/cloud/steam_cloud_types.hpp"

namespace cauth::steam::cloud {

SteamCloudFileListResult fetch_remote_file_list_via_cm(const SteamCloudRequest& request,
                                                       std::uint32_t count,
                                                       std::uint32_t start_index,
                                                       bool extended_details);
SteamCloudDownloadResult download_remote_file_via_cm(const SteamCloudRequest& request,
                                                     const SteamCloudFileEntry& file,
                                                     const SteamCloudDownloadOptions& download_options = {},
                                                     const cauth::core::platform::HttpRequestCallbacks& callbacks =
                                                         {});
SteamCloudUploadResult upload_cloud_files_via_cm(const SteamCloudRequest& request,
                                                 std::string_view machine_name,
                                                 const std::vector<SteamCloudUploadFile>& files,
                                                 const std::vector<std::string>& files_to_delete,
                                                 const SteamCloudUploadCallbacks& callbacks = {});

} // namespace cauth::steam::cloud
