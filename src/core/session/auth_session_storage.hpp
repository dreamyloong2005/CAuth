#ifndef CAUTH_CORE_SESSION_AUTH_SESSION_STORAGE_HPP
#define CAUTH_CORE_SESSION_AUTH_SESSION_STORAGE_HPP

#include "core/session/auth_session.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace cauth::core::session {

class AuthSessionWriter {
  public:
    virtual ~AuthSessionWriter() = default;

    virtual void save_auth_session(const AuthSession& session) = 0;
};

class AuthSessionReader {
  public:
    virtual ~AuthSessionReader() = default;

    virtual std::vector<AuthSession> list_auth_sessions() const = 0;
    virtual std::optional<AuthSession> load_auth_session(std::string_view provider,
                                                         std::string_view subject_id) const = 0;
};

class AuthSessionClearer {
  public:
    virtual ~AuthSessionClearer() = default;

    virtual void clear_auth_session(std::string_view provider, std::string_view subject_id) = 0;
    virtual void clear_all_auth_sessions() = 0;
};

} // namespace cauth::core::session

#endif
