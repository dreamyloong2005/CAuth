#ifndef CAUTH_STEAM_CM_STEAM_CM_CONNECTOR_HPP
#define CAUTH_STEAM_CM_STEAM_CM_CONNECTOR_HPP

#include "core/session/auth_session.hpp"
#include "steam/cm/cm_server.hpp"
#include "steam/cm/cm_service_method.hpp"
#include "steam/cm/cm_session.hpp"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>

namespace cauth::core::cm {

enum class SteamCmContinuation {
    Continue,
    Stop,
};

struct SteamCmAttemptResult {
    SteamCmContinuation continuation = SteamCmContinuation::Continue;
    bool ok = false;
    std::string error_message;
};

struct SteamCmOperationResult {
    bool ok = false;
    std::string error_message;
};

using SteamCmServiceClientOperation =
    std::function<SteamCmAttemptResult(const CmServerEndpoint&, CmServiceMethodClient&)>;
using SteamCmSessionOperation =
    std::function<SteamCmAttemptResult(const CmServerEndpoint&, CmSession&)>;

class SteamCmConnector {
  public:
    SteamCmConnector(std::ostream* out = nullptr, std::ostream* err = nullptr);

    SteamCmOperationResult with_service_client(std::uint32_t max_count,
                                               const SteamCmServiceClientOperation& operation) const;
    SteamCmOperationResult with_logged_on_session(
        const cauth::core::session::AuthSession& session,
        std::uint32_t max_count,
        const SteamCmSessionOperation& operation) const;

  private:
    std::ostream* out_;
    std::ostream* err_;
};

} // namespace cauth::core::cm

#endif
