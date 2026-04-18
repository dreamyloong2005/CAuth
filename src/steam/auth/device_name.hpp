#ifndef CAUTH_CORE_AUTH_DEVICE_NAME_HPP
#define CAUTH_CORE_AUTH_DEVICE_NAME_HPP

#include <string>
#include <string_view>

namespace cauth::steam::auth {

std::string normalize_device_name(std::string_view requested_name);

} // namespace cauth::steam::auth

#endif
