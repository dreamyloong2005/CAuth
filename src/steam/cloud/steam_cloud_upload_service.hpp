#pragma once

#include "core/platform/http_client.hpp"

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
};

struct SteamCloudWebAuthContext {
    std::string access_token;
    std::string web_cookie_header;
    std::string store_cookie_header;
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
                                          const std::vector<std::string>& files_to_delete);
SteamCloudUploadResult upload_cloud_files(const SteamCloudWebAuthContext& auth,
                                          std::uint32_t app_id,
                                          std::string_view machine_name,
                                          const std::vector<SteamCloudUploadFile>& files,
                                          const std::vector<std::string>& files_to_delete);

} // namespace cauth::steam::cloud
