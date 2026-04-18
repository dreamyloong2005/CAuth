#include "steam/depot/manifest_downloader.hpp"

#include "core/platform/http_client.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace cauth::core::depot {
namespace {

constexpr std::uint32_t kManifestVersion = 5;
constexpr std::int32_t kDownloadConnectTimeoutMs = 15000;

} // namespace

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

std::string build_manifest_path(const ManifestDownloadRequest& request) {
    std::ostringstream path;
    path << "depot/" << request.depot_id << "/manifest/" << request.manifest_gid << '/'
         << kManifestVersion;
    if (request.manifest_request_code != 0) {
        path << '/' << request.manifest_request_code;
    }
    return path.str();
}

std::string build_cdn_url(const CdnServer& server, const std::string& path);

std::string build_manifest_url(const CdnServer& server, const ManifestDownloadRequest& request) {
    return build_cdn_url(server, build_manifest_path(request));
}

std::string build_chunk_path(const ChunkDownloadRequest& request) {
    std::ostringstream path;
    path << "depot/" << request.depot_id << "/chunk/" << bytes_to_hex(request.chunk_sha);
    return path.str();
}

std::string build_chunk_url(const CdnServer& server, const ChunkDownloadRequest& request) {
    return build_cdn_url(server, build_chunk_path(request));
}

std::string build_cdn_url(const CdnServer& server, const std::string& path) {
    std::ostringstream url;
    url << (server.protocol == CdnServerProtocol::Https ? "https" : "http") << "://"
        << server.vhost;
    if ((server.protocol == CdnServerProtocol::Https && server.port != 443) ||
        (server.protocol == CdnServerProtocol::Http && server.port != 80)) {
        url << ':' << server.port;
    }
    url << '/' << path;
    return url.str();
}

ManifestDownloadResult ManifestDownloader::download_raw_manifest(
    const CdnServer& server,
    const ManifestDownloadRequest& request) const {
    platform::HttpRequest http_request;
    http_request.url = build_manifest_url(server, request);
    http_request.connect_timeout_ms = kDownloadConnectTimeoutMs;
    http_request.read_timeout_ms = 0;
    const auto response = platform::perform_platform_http_request(http_request);
    return {response.ok, response.error_message, http_request.url, std::move(response.body)};
}

ManifestDownloadResult ManifestDownloader::download_raw_chunk(
    const CdnServer& server,
    const ChunkDownloadRequest& request) const {
    platform::HttpRequest http_request;
    http_request.url = build_chunk_url(server, request);
    http_request.connect_timeout_ms = kDownloadConnectTimeoutMs;
    http_request.read_timeout_ms = 0;
    const auto response = platform::perform_platform_http_request(http_request);
    return {response.ok, response.error_message, http_request.url, std::move(response.body)};
}

} // namespace cauth::core::depot
