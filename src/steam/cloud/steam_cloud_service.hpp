#pragma once

#include "core/platform/http_client.hpp"
#include "steam/cloud/steam_cloud_types.hpp"

#include <string_view>

namespace cauth::steam::cloud {

SteamCloudRequest materialize_cloud_web_api_auth(const SteamCloudRequest& request,
                                                 std::string* error_message = nullptr);
std::string build_enumerate_user_files_url(const SteamCloudRequest& request,
                                           std::uint32_t count,
                                           std::uint32_t start_index,
                                           bool extended_details);
SteamCloudFileListResult parse_enumerate_user_files_response(std::uint32_t app_id,
                                                             std::string_view json,
                                                             std::uint32_t eresult = 1);
SteamCloudFileListResult fetch_remote_file_list_via_web_api(const SteamCloudRequest& request,
                                                            std::uint32_t count,
                                                            std::uint32_t start_index,
                                                            bool extended_details);
SteamCloudFileListResult fetch_remote_file_list(const SteamCloudRequest& request,
                                                std::uint32_t count,
                                                std::uint32_t start_index,
                                                bool extended_details);
SteamCloudDownloadResult download_remote_file(const SteamCloudRequest& request,
                                              const SteamCloudFileEntry& file,
                                              const cauth::core::platform::HttpRequestCallbacks& callbacks =
                                                  {});

} // namespace cauth::steam::cloud
