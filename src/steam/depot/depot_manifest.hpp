#ifndef CAUTH_CORE_DEPOT_DEPOT_MANIFEST_HPP
#define CAUTH_CORE_DEPOT_DEPOT_MANIFEST_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace cauth::core::depot {

inline constexpr std::uint32_t kDepotFileFlagDirectory = 1U << 6U;

struct DepotManifestChunk {
    std::vector<std::uint8_t> sha;
    std::uint32_t checksum = 0;
    std::uint64_t offset = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
};

struct DepotManifestFile {
    std::string filename;
    std::vector<std::uint8_t> filename_sha;
    std::vector<std::uint8_t> content_sha;
    std::string link_target;
    std::uint32_t flags = 0;
    std::uint64_t size = 0;
    std::vector<DepotManifestChunk> chunks;
};

struct DepotManifest {
    bool filenames_encrypted = false;
    std::uint32_t depot_id = 0;
    std::uint64_t manifest_gid = 0;
    std::uint32_t creation_time = 0;
    std::uint64_t total_uncompressed_size = 0;
    std::uint64_t total_compressed_size = 0;
    std::uint32_t unique_chunks = 0;
    std::uint32_t encrypted_crc = 0;
    std::uint32_t clear_crc = 0;
    std::vector<DepotManifestFile> files;
};

struct DepotManifestParseResult {
    bool ok = false;
    std::string error_message;
    DepotManifest manifest;
};

struct DepotManifestDecryptResult {
    bool ok = false;
    std::string error_message;
};

DepotManifestParseResult parse_depot_manifest(const std::vector<std::uint8_t>& bytes);
DepotManifestDecryptResult decrypt_depot_manifest_filenames(
    DepotManifest& manifest,
    const std::vector<std::uint8_t>& depot_key);
bool depot_file_is_directory(const DepotManifestFile& file);

} // namespace cauth::core::depot

#endif
