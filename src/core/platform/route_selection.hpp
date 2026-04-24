#pragma once

#include <string>
#include <string_view>

namespace cauth::core::platform {

struct RouteSelection {
    std::string endpoint;
    std::string protocol;
    std::string role;

    [[nodiscard]] bool empty() const {
        return endpoint.empty() && protocol.empty() && role.empty();
    }
};

inline bool route_selection_applies_to_role(const RouteSelection* selection,
                                            std::string_view role) {
    return selection == nullptr || selection->role.empty() || selection->role == role;
}

inline bool route_selection_matches(const RouteSelection* selection,
                                    std::string_view endpoint,
                                    std::string_view protocol,
                                    std::string_view role = {}) {
    if (selection == nullptr || selection->empty()) {
        return true;
    }
    if (!selection->endpoint.empty() && selection->endpoint != endpoint) {
        return false;
    }
    if (!selection->protocol.empty() && selection->protocol != protocol) {
        return false;
    }
    if (!selection->role.empty() && selection->role != role) {
        return false;
    }
    return true;
}

} // namespace cauth::core::platform
