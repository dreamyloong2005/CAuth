#ifndef CAUTH_STEAM_CM_STEAM_CM_CONNECTOR_HPP
#define CAUTH_STEAM_CM_STEAM_CM_CONNECTOR_HPP

#include "core/session/auth_session.hpp"
#include "core/platform/endpoint_route_cache.hpp"
#include "core/platform/route_selection.hpp"
#include "steam/cm/cm_server.hpp"
#include "steam/cm/cm_service_method.hpp"
#include "steam/cm/cm_session.hpp"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

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

struct SteamCmRouteEntry {
    CmServerEndpoint endpoint;
    cauth::core::platform::ProbedRouteEntry route;
};

struct SteamCmRouteReport {
    bool ok = false;
    std::string module_status = "idle";
    std::string message;
    std::vector<SteamCmRouteEntry> routes;
};

using SteamCmServiceClientOperation =
    std::function<SteamCmAttemptResult(const CmServerEndpoint&, CmServiceMethodClient&)>;
using SteamCmSessionOperation =
    std::function<SteamCmAttemptResult(const CmServerEndpoint&, CmSession&)>;

SteamCmRouteReport probe_websocket_routes(
    std::uint32_t max_count,
    std::ostream* err = nullptr,
    const cauth::core::platform::RouteSelection* route_selection = nullptr);

class SteamCmConnector {
  public:
    SteamCmConnector(std::ostream* out = nullptr, std::ostream* err = nullptr);

    SteamCmOperationResult with_service_client(std::uint32_t max_count,
                                               const cauth::core::platform::RouteSelection* route_selection,
                                               const SteamCmServiceClientOperation& operation) const;
    SteamCmOperationResult with_logged_on_session(
        const cauth::core::session::AuthSession& session,
        std::uint32_t max_count,
        const cauth::core::platform::RouteSelection* route_selection,
        const SteamCmSessionOperation& operation) const;

  private:
    std::ostream* out_;
    std::ostream* err_;
};

} // namespace cauth::core::cm

#endif
