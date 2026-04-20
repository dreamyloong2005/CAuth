#include "core/session/auth_session_codec.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace cauth::core::session {
namespace {

constexpr std::array<std::uint8_t, 12> kMagicV1 = {'C', 'A', 'U', 'T', 'H', 'S',
                                                   'E', 'S', 'S', '1', '\r', '\n'};
constexpr std::array<std::uint8_t, 12> kMagicV2 = {'C', 'A', 'U', 'T', 'H', 'S',
                                                   'E', 'S', 'S', '2', '\r', '\n'};
constexpr std::array<std::uint8_t, 12> kMagicV3 = {'C', 'A', 'U', 'T', 'H', 'S',
                                                   'E', 'S', 'S', '3', '\r', '\n'};
constexpr std::array<std::uint8_t, 12> kMagicStoreV1 = {'C', 'A', 'U', 'T', 'H', 'A',
                                                        'C', 'C', 'T', '1', '\r', '\n'};
constexpr std::string_view kLegacyV1Provider = "steam";

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffULL));
    }
}

bool read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint32_t& value) {
    if (bytes.size() - offset < sizeof(std::uint32_t)) {
        return false;
    }

    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    }
    return true;
}

bool read_u64(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint64_t& value) {
    if (bytes.size() - offset < sizeof(std::uint64_t)) {
        return false;
    }

    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    }
    return true;
}

bool append_string(std::vector<std::uint8_t>& out, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    append_u32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

bool read_string(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::string& value) {
    std::uint32_t length = 0;
    if (!read_u32(bytes, offset, length) || bytes.size() - offset < length) {
        return false;
    }

    value.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
    offset += length;
    return true;
}

bool decode_v1(const std::vector<std::uint8_t>& bytes, AuthSession& session) {
    std::size_t offset = kMagicV1.size();
    std::uint64_t legacy_steam_id = 0;
    std::uint64_t created_at_seconds = 0;

    if (!read_u64(bytes, offset, legacy_steam_id) ||
        !read_u64(bytes, offset, created_at_seconds) ||
        !read_string(bytes, offset, session.account_name) ||
        !read_string(bytes, offset, session.refresh_token)) {
        return false;
    }

    if (offset < bytes.size() && !read_string(bytes, offset, session.access_token)) {
        return false;
    }

    if (offset != bytes.size()) {
        return false;
    }

    session.created_at =
        std::chrono::system_clock::time_point{std::chrono::seconds{created_at_seconds}};
    session.provider = std::string{kLegacyV1Provider};
    session.subject_id = std::to_string(legacy_steam_id);
    return true;
}

bool decode_v2(const std::vector<std::uint8_t>& bytes, AuthSession& session) {
    std::size_t offset = kMagicV2.size();
    std::uint64_t created_at_seconds = 0;

    if (!read_u64(bytes, offset, created_at_seconds) ||
        !read_string(bytes, offset, session.provider) ||
        !read_string(bytes, offset, session.subject_id) ||
        !read_string(bytes, offset, session.account_name) ||
        !read_string(bytes, offset, session.refresh_token)) {
        return false;
    }

    if (offset < bytes.size() && !read_string(bytes, offset, session.access_token)) {
        return false;
    }

    if (offset != bytes.size()) {
        return false;
    }

    session.created_at =
        std::chrono::system_clock::time_point{std::chrono::seconds{created_at_seconds}};
    return true;
}

bool decode_v3(const std::vector<std::uint8_t>& bytes, AuthSession& session) {
    std::size_t offset = kMagicV3.size();
    std::uint64_t created_at_seconds = 0;

    if (!read_u64(bytes, offset, created_at_seconds) ||
        !read_string(bytes, offset, session.provider) ||
        !read_string(bytes, offset, session.subject_id) ||
        !read_string(bytes, offset, session.account_name) ||
        !read_string(bytes, offset, session.refresh_token) ||
        !read_string(bytes, offset, session.access_token) ||
        !read_string(bytes, offset, session.session_type)) {
        return false;
    }

    if (offset != bytes.size()) {
        return false;
    }

    session.created_at =
        std::chrono::system_clock::time_point{std::chrono::seconds{created_at_seconds}};
    return true;
}

bool append_session(std::vector<std::uint8_t>& out, const AuthSession& session) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                             session.created_at.time_since_epoch())
                             .count();
    append_u64(out, static_cast<std::uint64_t>(seconds));
    return append_string(out, session.provider) &&
           append_string(out, session.subject_id) &&
           append_string(out, session.account_name) &&
           append_string(out, session.refresh_token) &&
           append_string(out, session.access_token) &&
           append_string(out, session.session_type);
}

bool read_session(const std::vector<std::uint8_t>& bytes,
                  std::size_t& offset,
                  AuthSession& session) {
    std::uint64_t created_at_seconds = 0;
    if (!read_u64(bytes, offset, created_at_seconds) ||
        !read_string(bytes, offset, session.provider) ||
        !read_string(bytes, offset, session.subject_id) ||
        !read_string(bytes, offset, session.account_name) ||
        !read_string(bytes, offset, session.refresh_token) ||
        !read_string(bytes, offset, session.access_token) ||
        !read_string(bytes, offset, session.session_type)) {
        return false;
    }
    session.created_at =
        std::chrono::system_clock::time_point{std::chrono::seconds{created_at_seconds}};
    return true;
}

bool decode_store_v1(const std::vector<std::uint8_t>& bytes,
                     AuthSessionRepositoryState& state) {
    std::size_t offset = kMagicStoreV1.size();
    AuthSessionKey active;
    std::uint32_t count = 0;

    if (!read_string(bytes, offset, active.provider) ||
        !read_string(bytes, offset, active.subject_id) ||
        !read_u32(bytes, offset, count)) {
        return false;
    }

    constexpr std::uint32_t kMaxAccounts = 1024;
    if (count > kMaxAccounts) {
        return false;
    }

    for (std::uint32_t index = 0; index < count; ++index) {
        AuthSession session;
        if (!read_session(bytes, offset, session)) {
            return false;
        }
        upsert_auth_session(state, session, false);
    }

    if (offset != bytes.size()) {
        return false;
    }

    if (is_valid(active) && find_auth_session(state, active).has_value()) {
        state.active = std::move(active);
    }
    normalize_auth_session_repository_state(state);
    return true;
}

} // namespace

std::vector<std::uint8_t> encode_auth_session(const AuthSession& session) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), kMagicV3.begin(), kMagicV3.end());

    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                             session.created_at.time_since_epoch())
                             .count();
    append_u64(out, static_cast<std::uint64_t>(seconds));

    if (!append_string(out, session.provider) || !append_string(out, session.subject_id) ||
        !append_string(out, session.account_name) || !append_string(out, session.refresh_token) ||
        !append_string(out, session.access_token) || !append_string(out, session.session_type)) {
        return {};
    }

    return out;
}

std::optional<AuthSession> decode_auth_session(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kMagicV1.size()) {
        return std::nullopt;
    }

    AuthSession session;
    if (std::equal(kMagicV3.begin(), kMagicV3.end(), bytes.begin())) {
        if (!decode_v3(bytes, session)) {
            return std::nullopt;
        }
    } else if (std::equal(kMagicV2.begin(), kMagicV2.end(), bytes.begin())) {
        if (!decode_v2(bytes, session)) {
            return std::nullopt;
        }
    } else if (std::equal(kMagicV1.begin(), kMagicV1.end(), bytes.begin())) {
        if (!decode_v1(bytes, session)) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    if (!is_valid(session)) {
        return std::nullopt;
    }

    return session;
}

std::vector<std::uint8_t> encode_auth_session_repository_state(
    const AuthSessionRepositoryState& state) {
    AuthSessionRepositoryState normalized = state;
    normalize_auth_session_repository_state(normalized);

    std::vector<std::uint8_t> out;
    out.insert(out.end(), kMagicStoreV1.begin(), kMagicStoreV1.end());

    const AuthSessionKey active = normalized.active.value_or(AuthSessionKey{});
    if (!append_string(out, active.provider) ||
        !append_string(out, active.subject_id)) {
        return {};
    }

    if (normalized.sessions.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }
    append_u32(out, static_cast<std::uint32_t>(normalized.sessions.size()));
    for (const auto& session : normalized.sessions) {
        if (!append_session(out, session)) {
            return {};
        }
    }

    return out;
}

std::optional<AuthSessionRepositoryState> decode_auth_session_repository_state(
    const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kMagicV1.size()) {
        return std::nullopt;
    }

    AuthSessionRepositoryState state;
    if (std::equal(kMagicStoreV1.begin(), kMagicStoreV1.end(), bytes.begin())) {
        if (!decode_store_v1(bytes, state)) {
            return std::nullopt;
        }
        return state;
    }

    const auto legacy = decode_auth_session(bytes);
    if (!legacy.has_value()) {
        return std::nullopt;
    }
    upsert_auth_session(state, *legacy);
    return state;
}

} // namespace cauth::core::session
