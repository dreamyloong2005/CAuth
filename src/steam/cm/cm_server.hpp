#ifndef CAUTH_CORE_CM_CM_SERVER_HPP
#define CAUTH_CORE_CM_CM_SERVER_HPP

#include <cstdint>
#include <string>

namespace cauth::core::cm {

enum class CmServerProtocol {
    Tcp,
    WebSocket,
};

struct CmServerEndpoint {
    std::string address;
    std::uint16_t port = 0;
    CmServerProtocol protocol = CmServerProtocol::Tcp;
};

} // namespace cauth::core::cm

#endif
