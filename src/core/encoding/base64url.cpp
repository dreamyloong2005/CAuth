#include "core/encoding/base64url.hpp"

#include <cctype>

namespace cauth::core::encoding {

std::optional<std::vector<std::uint8_t>> base64url_decode(std::string_view value) {
    auto decode_char = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '-' || ch == '+') return 62;
        if (ch == '_' || ch == '/') return 63;
        return -1;
    };

    std::vector<std::uint8_t> out;
    int accumulator = 0;
    int bits = -8;
    for (const auto ch : value) {
        if (ch == '=') break;
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) continue;
        const auto decoded = decode_char(ch);
        if (decoded < 0) return std::nullopt;
        accumulator = (accumulator << 6) | decoded;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}

} // namespace cauth::core::encoding
