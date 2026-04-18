#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace cauth::core::encoding {

std::optional<std::vector<std::uint8_t>> base64url_decode(std::string_view value);

} // namespace cauth::core::encoding
