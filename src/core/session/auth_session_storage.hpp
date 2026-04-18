#ifndef CAUTH_CORE_SESSION_AUTH_SESSION_STORAGE_HPP
#define CAUTH_CORE_SESSION_AUTH_SESSION_STORAGE_HPP

#include "core/session/auth_session.hpp"

#include <optional>

namespace cauth::core::session {

class AuthSessionWriter {
  public:
    virtual ~AuthSessionWriter() = default;

    virtual void save_auth_session(const AuthSession& session) = 0;
};

class AuthSessionReader {
  public:
    virtual ~AuthSessionReader() = default;

    virtual std::optional<AuthSession> load_auth_session() const = 0;
};

class AuthSessionClearer {
  public:
    virtual ~AuthSessionClearer() = default;

    virtual void clear_auth_session() = 0;
};

} // namespace cauth::core::session

#endif
