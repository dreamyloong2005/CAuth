#ifndef CAUTH_CORE_DEPOT_CDN_DIRECTORY_HPP
#define CAUTH_CORE_DEPOT_CDN_DIRECTORY_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::core::depot {

enum class CdnServerProtocol {
    Http,
    Https,
};

struct CdnServer {
    CdnServerProtocol protocol = CdnServerProtocol::Https;
    std::string host;
    std::string vhost;
    std::uint16_t port = 443;
    std::string type;
    bool use_as_proxy = false;
    std::string proxy_request_path_template;
};

struct CdnServerListResult {
    bool ok = false;
    std::string error_message;
    std::vector<CdnServer> servers;
};

struct CdnServerQuery {
    std::uint32_t cell_id = 0;
    std::uint32_t max_servers = 20;
};

class CdnDirectoryClient {
  public:
    CdnServerListResult get_servers_for_steampipe(const CdnServerQuery& query) const;
};

std::vector<CdnServer> parse_cdn_server_list_response(std::string_view json);

} // namespace cauth::core::depot

#endif
