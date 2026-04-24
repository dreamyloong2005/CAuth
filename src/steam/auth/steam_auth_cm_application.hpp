#pragma once

#include "core/platform/route_selection.hpp"
#include "steam/cm/steam_directory.hpp"

#include <cstdint>
#include <iosfwd>

namespace cauth::steam::auth {

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
int run_cm_app_info(const cauth::core::cm::CmServerQuery& query,
                    std::uint64_t steam_id,
                    std::uint32_t app_id,
                    bool debug_app_info,
                    const cauth::core::platform::RouteSelection* route_selection,
                    std::ostream& out,
                    std::ostream& err);

} // namespace cauth::steam::auth
