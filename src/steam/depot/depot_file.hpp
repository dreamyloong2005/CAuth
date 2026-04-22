#ifndef CAUTH_CORE_DEPOT_DEPOT_FILE_HPP
#define CAUTH_CORE_DEPOT_DEPOT_FILE_HPP

#include "steam/depot/depot_manifest.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::core::depot {

struct DepotManifestPathResult {
    bool ok = false;
    std::string error_message;
    std::string normalized_path;
    std::filesystem::path relative_path;
};

struct DepotFileResult {
    bool ok = false;
    std::string error_message;
};

DepotManifestPathResult normalize_depot_manifest_path(std::string_view path);

std::string depot_manifest_path_for_display(std::string_view path);

DepotFileResult validate_depot_file_layout(const DepotManifestFile& file);

std::optional<std::size_t> find_depot_file_index(const DepotManifest& manifest,
                                                 std::string_view filename);

DepotFileResult write_depot_file_chunk(std::ostream& output,
                                       const DepotManifestFile& file,
                                       std::size_t chunk_index,
                                       const std::vector<std::uint8_t>& chunk_bytes);

DepotFileResult verify_depot_file_on_disk(const std::filesystem::path& path,
                                          const DepotManifestFile& file);

DepotFileResult verify_depot_file_chunk_checksums(const std::filesystem::path& path,
                                                  const DepotManifestFile& file);

DepotFileResult verify_depot_file_content_sha(const std::filesystem::path& path,
                                              const DepotManifestFile& file);

bool depot_file_has_binary_verification(const DepotManifestFile& file);

} // namespace cauth::core::depot

#endif
