#ifndef CAUTH_CORE_SESSION_SESSION_REPOSITORY_HPP
#define CAUTH_CORE_SESSION_SESSION_REPOSITORY_HPP

#include "core/session/auth_session_storage.hpp"

namespace cauth::core::session {

class SessionRepository : public AuthSessionWriter,
                          public AuthSessionReader,
                          public AuthSessionClearer {
  public:
    virtual ~SessionRepository() = default;
};

} // namespace cauth::core::session

#endif
