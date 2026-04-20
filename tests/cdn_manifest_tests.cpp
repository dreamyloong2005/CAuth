#include "steam/depot/cdn_directory.hpp"
#include "steam/depot/manifest_downloader.hpp"

#include <iostream>

int main() {
    const auto servers = cauth::core::depot::parse_cdn_server_list_response(
        R"({"response":{"servers":[{"host":"cache1.example.com","vhost":"cdn.example.com","https_support":"mandatory","type":"CDN","use_as_proxy":false},{"host":"cache2.example.com","https_support":"optional","type":"CDN"}]}})");
    if (servers.size() != 2 || servers[0].vhost != "cdn.example.com" ||
        servers[0].protocol != cauth::core::depot::CdnServerProtocol::Https ||
        servers[0].port != 443 || servers[1].vhost != "cache2.example.com" ||
        servers[1].protocol != cauth::core::depot::CdnServerProtocol::Https ||
        servers[1].port != 443) {
        std::cerr << "CDN directory response should parse server endpoints\n";
        return 1;
    }

    const cauth::core::depot::ManifestDownloadRequest request{
        441,
        257913086909807568ULL,
        9096547788706308835ULL,
    };
    if (cauth::core::depot::build_manifest_path(request) !=
        "depot/441/manifest/257913086909807568/5/9096547788706308835") {
        std::cerr << "manifest path should include version and request code\n";
        return 1;
    }
    if (cauth::core::depot::build_manifest_url(servers[0], request) !=
        "https://cdn.example.com/depot/441/manifest/257913086909807568/5/"
        "9096547788706308835") {
        std::cerr << "manifest URL should use CDN vhost and manifest path\n";
        return 1;
    }

    const cauth::core::depot::ChunkDownloadRequest chunk_request{
        441,
        std::vector<std::uint8_t>{0x01, 0xab, 0xff},
    };
    if (cauth::core::depot::build_chunk_path(chunk_request) !=
        "depot/441/chunk/01abff") {
        std::cerr << "chunk path should include depot and hex chunk id\n";
        return 1;
    }
    if (cauth::core::depot::build_chunk_url(servers[0], chunk_request) !=
        "https://cdn.example.com/depot/441/chunk/01abff") {
        std::cerr << "chunk URL should use CDN vhost and chunk path\n";
        return 1;
    }

    return 0;
}
