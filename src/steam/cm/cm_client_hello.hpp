#ifndef CAUTH_CORE_CM_CM_CLIENT_HELLO_HPP
#define CAUTH_CORE_CM_CM_CLIENT_HELLO_HPP

#include "steam/cm/cm_message.hpp"

#include <cstdint>

namespace cauth::core::cm {

CmMessage make_client_hello_message(std::uint32_t protocol_version = 65580);

} // namespace cauth::core::cm

#endif
