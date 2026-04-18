#include "cli/cli_support.hpp"

#include <cctype>
#include <cstdlib>
#include <memory>

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

} // namespace cauth::cli::support
