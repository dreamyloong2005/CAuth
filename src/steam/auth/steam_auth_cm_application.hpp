#pragma once

#include "core/platform/route_selection.hpp"
#include "steam/cm/steam_directory.hpp"

#include <cstdint>
#include <functional>
#include <iosfwd>

namespace cauth::core::cm {
class CmSession;
} // namespace cauth::core::cm

namespace cauth::steam::auth {

// Runs on the logged-on CM session after a successful logon and heartbeat.
// Its return value becomes the command's exit code.
using CmLogonContinuation =
    std::function<int(cauth::core::cm::CmSession& session, std::ostream& out, std::ostream& err)>;

int run_cm_frame_test(std::ostream& out, std::ostream& err);
int run_cm_servers(const cauth::core::cm::CmServerQuery& query,
                   std::ostream& out,
                   std::ostream& err);
int run_cm_routes(const cauth::core::cm::CmServerQuery& query,
                  const cauth::core::platform::RouteSelection* route_selection,
                  std::ostream& out,
                  std::ostream& err);
int run_cm_probe(const cauth::core::cm::CmServerQuery& query,
                 const cauth::core::platform::RouteSelection* route_selection,
                 std::ostream& out,
                 std::ostream& err);
int run_cm_logon(const cauth::core::cm::CmServerQuery& query,
                 std::uint64_t steam_id,
                 const cauth::core::platform::RouteSelection* route_selection,
                 std::ostream& out,
                 std::ostream& err);
// Logs on to CM with the saved account, sends a heartbeat, then runs `after_logon`
// (when set) on the session. Returns 0 on success.
int run_cm_logon_then(const cauth::core::cm::CmServerQuery& query,
                      std::uint64_t steam_id,
                      const cauth::core::platform::RouteSelection* route_selection,
                      const CmLogonContinuation& after_logon,
                      std::ostream& out,
                      std::ostream& err);

} // namespace cauth::steam::auth
