#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::cli::support {

bool env_flag_enabled(const char* name);

std::optional<std::vector<std::uint8_t>> hex_to_bytes(std::string_view hex);
std::uint64_t parse_u64_arg(const char* value);

} // namespace cauth::cli::support
