#ifndef CAUTH_CORE_CM_CM_HEARTBEAT_HPP
#define CAUTH_CORE_CM_CM_HEARTBEAT_HPP

#include "core/session/auth_session.hpp"
#include "steam/cm/cm_message.hpp"

namespace cauth::core::cm {

CmMessage make_client_heartbeat_message(const session::AuthSession& session);

} // namespace cauth::core::cm

#endif
