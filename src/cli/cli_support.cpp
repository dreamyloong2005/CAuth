#include "cli/cli_support.hpp"

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace cauth::cli::support {
namespace {

} // namespace

bool env_flag_enabled(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t value_length = 0;
    if (_dupenv_s(&value, &value_length, name) != 0 || value == nullptr) {
        return false;
    }
    std::unique_ptr<char, decltype(&std::free)> value_guard{value, std::free};
    return value_length > 0 && std::string_view{value_guard.get()} == "1";
#else
    const auto* value = std::getenv(name);
    return value != nullptr && std::string_view{value} == "1";
#endif
}

std::optional<std::vector<std::uint8_t>> hex_to_bytes(std::string_view hex) {
    if (hex.empty() || hex.size() % 2 != 0) return std::nullopt;
    auto hex_value = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t index = 0; index < hex.size(); index += 2) {
        const auto high = hex_value(hex[index]);
        const auto low = hex_value(hex[index + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return bytes;
}

std::uint64_t parse_u64_arg(const char* value) {
    if (value == nullptr || *value == '\0') return 0;
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    if (end == value || (end != nullptr && *end != '\0')) return 0;
    return static_cast<std::uint64_t>(parsed);
}

std::string format_byte_count(std::uint64_t bytes) {
    static constexpr const char* kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    constexpr std::size_t kUnitCount = sizeof(kUnits) / sizeof(kUnits[0]);
    double value = static_cast<double>(bytes);
    std::size_t unit_index = 0;
    while (value >= 1024.0 && unit_index + 1 < kUnitCount) {
        value /= 1024.0;
        ++unit_index;
    }

    std::ostringstream out;
    if (unit_index == 0) {
        out << bytes << ' ' << kUnits[unit_index];
    } else {
        out << std::fixed << std::setprecision(value >= 100.0 ? 0 : value >= 10.0 ? 1 : 2)
            << value << ' ' << kUnits[unit_index];
    }
    return out.str();
}

std::string truncate_progress_text(std::string_view text, std::size_t max_length) {
    if (text.size() <= max_length) {
        return std::string{text};
    }
    if (max_length <= 3) {
        return std::string{text.substr(0, max_length)};
    }
    return std::string{text.substr(0, max_length - 3)} + "...";
}

void print_progress_line(std::ostream& out, ProgressLineState& state, std::string_view line) {
    out << '\r' << line;
    if (state.last_line_length > line.size()) {
        out << std::string(state.last_line_length - line.size(), ' ');
    }
    out.flush();
    state.last_line_length = line.size();
    state.active = true;
}

void finish_progress_line(std::ostream& out, ProgressLineState& state) {
    if (!state.active) {
        return;
    }
    out << '\n';
    out.flush();
    state.last_line_length = 0;
    state.active = false;
}

} // namespace cauth::cli::support
