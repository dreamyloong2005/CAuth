#ifndef CAUTH_CORE_SESSION_AUTH_SESSION_CODEC_HPP
#define CAUTH_CORE_SESSION_AUTH_SESSION_CODEC_HPP

#include "core/session/auth_session.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace cauth::core::session {

std::vector<std::uint8_t> encode_auth_session(const AuthSession& session);
std::optional<AuthSession> decode_auth_session(const std::vector<std::uint8_t>& bytes);

} // namespace cauth::core::session

#endif
