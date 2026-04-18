#include "steam/auth/device_name.hpp"

namespace cauth::steam::auth {

std::string normalize_device_name(std::string_view requested_name) {
    if (requested_name.empty()) {
        return "CAuth";
    }

    return std::string{requested_name} + "_CAuth";
}

} // namespace cauth::steam::auth
