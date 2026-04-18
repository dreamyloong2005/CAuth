#ifndef CAUTH_CORE_CM_STEAM_DIRECTORY_HPP
#define CAUTH_CORE_CM_STEAM_DIRECTORY_HPP

#include "steam/cm/cm_server.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::core::cm {

struct CmServerQuery {
    std::uint32_t cell_id = 0;
    std::uint32_t max_count = 20;
    CmServerProtocol protocol = CmServerProtocol::WebSocket;
};

struct CmServerListResult {
    bool ok = false;
    std::string error_message;
    std::vector<CmServerEndpoint> servers;
};

class SteamDirectoryClient {
  public:
    CmServerListResult get_cm_servers(const CmServerQuery& query) const;
};

std::vector<CmServerEndpoint> parse_cm_server_list_response(std::string_view json,
                                                            CmServerProtocol protocol);

} // namespace cauth::core::cm

#endif
