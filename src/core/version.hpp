#ifndef CAUTH_CORE_VERSION_HPP
#define CAUTH_CORE_VERSION_HPP

#include <string_view>

namespace cauth::core {

struct Version {
    int major;
    int minor;
    int patch;
    std::string_view text;
};

Version version() noexcept;

} // namespace cauth::core

#endif
