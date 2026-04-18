#pragma once

#include "steam/depot/app_info.hpp"
#include "steam/depot/depot_manifest.hpp"
#include "steam/depot/depot_key.hpp"
#include "steam/depot/manifest_request_code.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::steam::depot {

enum class DepotDownloadKind {
    Manifest = 1,
    Chunk = 2,
    File = 3,
    AllFiles = 4,
};

struct DepotDownloadProgress {
    DepotDownloadKind kind = DepotDownloadKind::Manifest;
    std::string phase;
    std::string target;
    std::uint64_t completed_steps = 0;
    std::uint64_t total_steps = 0;
    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
};

using DepotDownloadProgressHook = void (*)(const DepotDownloadProgress& progress, void* user_data);
using DepotDownloadCancelHook = bool (*)(void* user_data);

struct LoadedDepotManifest {
    cauth::core::depot::DepotManifest manifest;
    std::optional<std::vector<std::uint8_t>> depot_key;
};

struct LocalVerifyReport {
    bool fatal_error = false;
    std::uint64_t checked_count = 0;
    std::uint64_t ok_count = 0;
    std::uint64_t missing_count = 0;
    std::uint64_t mismatched_count = 0;
    std::uint64_t size_only_count = 0;
    std::uint64_t filtered_out_count = 0;
    std::uint64_t total_count = 0;

    [[nodiscard]] bool clean() const {
        return missing_count == 0 && mismatched_count == 0;
    }
};

void set_current_thread_depot_download_hooks(DepotDownloadProgressHook progress_hook,
                                             DepotDownloadCancelHook cancel_hook,
                                             void* user_data);
void clear_current_thread_depot_download_hooks();

std::optional<cauth::core::depot::AppInfo> fetch_app_info_from_cm(std::uint32_t app_id,
                                                                  std::uint32_t max_count,
                                                                  std::ostream& out,
                                                                  std::ostream& err);
std::optional<cauth::core::depot::DepotDecryptionKeyResponse> fetch_depot_key_from_cm(
    std::uint32_t app_id,
    std::uint32_t depot_id,
    std::uint32_t max_count,
    std::ostream& out,
    std::ostream& err);
std::optional<cauth::core::depot::ManifestRequestCodeResponse>
fetch_manifest_request_code_from_cm(
    const cauth::core::depot::ManifestRequestCodeRequest& request,
    std::uint32_t max_count,
    std::ostream& out,
    std::ostream& err);
int print_branches(std::uint32_t app_id,
                   std::uint32_t max_count,
                   std::ostream& out,
                   std::ostream& err);
int print_manifests(std::uint32_t app_id,
                    std::string_view branch,
                    std::uint32_t max_count,
                    std::ostream& out,
                    std::ostream& err);
int print_preflight(std::uint32_t app_id,
                    std::string_view branch,
                    std::uint32_t max_count,
                    std::ostream& out,
                    std::ostream& err);
int print_depot_key(std::uint32_t app_id,
                    std::uint32_t depot_id,
                    std::uint32_t max_count,
                    std::ostream& out,
                    std::ostream& err);
int print_manifest_request_code(std::uint32_t app_id,
                                std::uint32_t depot_id,
                                std::uint64_t manifest_gid,
                                std::string_view branch,
                                std::uint32_t max_count,
                                std::ostream& out,
                                std::ostream& err);
bool load_manifest_from_path(const std::string& input_path,
                             const std::optional<std::vector<std::uint8_t>>& depot_key,
                             LoadedDepotManifest& loaded_manifest,
                             std::ostream& err);
int print_manifest_info(const LoadedDepotManifest& loaded_manifest, std::ostream& out);
int print_file_list(const LoadedDepotManifest& loaded_manifest,
                    std::string_view filter_text,
                    std::size_t list_limit,
                    std::ostream& out);
int verify_local_files_against_manifest(const LoadedDepotManifest& loaded_manifest,
                                        const std::string& local_root,
                                        std::string_view filter_text,
                                        std::ostream& out,
                                        std::ostream& err,
                                        LocalVerifyReport* report = nullptr);
std::optional<std::size_t> resolve_file_selection(const LoadedDepotManifest& loaded_manifest,
                                                  std::size_t file_index,
                                                  bool has_file_index,
                                                  std::string_view file_path,
                                                  std::ostream& err);
bool validate_chunk_selection(const LoadedDepotManifest& loaded_manifest,
                              std::size_t file_index,
                              std::size_t chunk_index,
                              std::ostream& err);
int download_manifest_to_path(std::uint32_t depot_id,
                              std::uint64_t manifest_gid,
                              std::uint64_t request_code,
                              std::uint32_t max_count,
                              const std::string& output_path,
                              std::ostream& out,
                              std::ostream& err);
int download_chunk_from_manifest(const LoadedDepotManifest& loaded_manifest,
                                 std::size_t file_index,
                                 std::size_t chunk_index,
                                 bool process_chunk,
                                 std::uint32_t max_count,
                                 const std::string& output_path,
                                 std::ostream& out,
                                 std::ostream& err);
int download_file_from_manifest(const LoadedDepotManifest& loaded_manifest,
                                std::size_t file_index,
                                std::uint32_t max_count,
                                const std::string& output_path,
                                std::ostream& out,
                                std::ostream& err);
int download_all_files_from_manifest(const LoadedDepotManifest& loaded_manifest,
                                     std::uint32_t max_count,
                                     const std::string& output_root,
                                     std::ostream& out,
                                     std::ostream& err);

} // namespace cauth::steam::depot
