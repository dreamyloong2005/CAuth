#include "core/auth/jwt.hpp"

#include "core/encoding/base64url.hpp"

namespace cauth::core::auth {

std::optional<std::string> decode_jwt_payload(std::string_view token) {
    const auto first_dot = token.find('.');
    if (first_dot == std::string_view::npos) return std::nullopt;
    const auto second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) return std::nullopt;
    const auto payload = token.substr(first_dot + 1, second_dot - first_dot - 1);
    const auto decoded = cauth::core::encoding::base64url_decode(payload);
    if (!decoded.has_value()) return std::nullopt;
    return std::string{decoded->begin(), decoded->end()};
}

} // namespace cauth::core::auth
