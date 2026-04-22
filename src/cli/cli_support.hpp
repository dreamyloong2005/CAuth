#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::cli::support {

struct ProgressLineState {
    std::size_t last_line_length = 0;
    bool active = false;
};

bool env_flag_enabled(const char* name);

std::optional<std::vector<std::uint8_t>> hex_to_bytes(std::string_view hex);
std::uint64_t parse_u64_arg(const char* value);
std::string format_byte_count(std::uint64_t bytes);
std::string truncate_progress_text(std::string_view text, std::size_t max_length = 72);
void print_progress_line(std::ostream& out, ProgressLineState& state, std::string_view line);
void finish_progress_line(std::ostream& out, ProgressLineState& state);

} // namespace cauth::cli::support
