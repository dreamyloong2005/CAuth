#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cauth::core::auth {

std::optional<std::string> decode_jwt_payload(std::string_view token);

} // namespace cauth::core::auth
