#ifndef CAUTH_CORE_SESSION_SESSION_REPOSITORY_HPP
#define CAUTH_CORE_SESSION_SESSION_REPOSITORY_HPP

#include "core/session/auth_session_storage.hpp"

#include <string_view>
#include <vector>

namespace cauth::core::session {

class SessionRepository : public AuthSessionWriter,
                          public AuthSessionReader,
                          public AuthSessionClearer {
  public:
    using AuthSessionWriter::save_auth_session;

    virtual ~SessionRepository() = default;
};

} // namespace cauth::core::session

#endif
