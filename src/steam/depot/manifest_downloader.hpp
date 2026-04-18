#ifndef CAUTH_CORE_DEPOT_MANIFEST_DOWNLOADER_HPP
#define CAUTH_CORE_DEPOT_MANIFEST_DOWNLOADER_HPP

#include "steam/depot/cdn_directory.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::core::depot {

struct ManifestDownloadRequest {
    std::uint32_t depot_id = 0;
    std::uint64_t manifest_gid = 0;
    std::uint64_t manifest_request_code = 0;
};

struct ManifestDownloadResult {
    bool ok = false;
    std::string error_message;
    std::string url;
    std::vector<std::uint8_t> bytes;
};

struct ChunkDownloadRequest {
    std::uint32_t depot_id = 0;
    std::vector<std::uint8_t> chunk_sha;
};

std::string build_manifest_path(const ManifestDownloadRequest& request);
std::string build_manifest_url(const CdnServer& server, const ManifestDownloadRequest& request);
std::string build_chunk_path(const ChunkDownloadRequest& request);
std::string build_chunk_url(const CdnServer& server, const ChunkDownloadRequest& request);

class ManifestDownloader {
  public:
    ManifestDownloadResult download_raw_manifest(const CdnServer& server,
                                                 const ManifestDownloadRequest& request) const;
    ManifestDownloadResult download_raw_chunk(const CdnServer& server,
                                              const ChunkDownloadRequest& request) const;
};

} // namespace cauth::core::depot

#endif
