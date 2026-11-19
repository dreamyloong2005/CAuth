#pragma once

#include "core/platform/route_selection.hpp"
#include "steam/cm/steam_directory.hpp"

#include <cstdint>
#include <iosfwd>

namespace cauth::steam::depot {

// Logs on to CM with the saved account, requests PICS product info for `app_id`
// and prints the parsed branches, depots and manifests.
int run_cm_app_info(const cauth::core::cm::CmServerQuery& query,
                    std::uint64_t steam_id,
                    std::uint32_t app_id,
                    bool debug_app_info,
                    const cauth::core::platform::RouteSelection* route_selection,
                    std::ostream& out,
                    std::ostream& err);

} // namespace cauth::steam::depot
