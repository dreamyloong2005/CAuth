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
    using AuthSessionClearer::clear_auth_session;
    using AuthSessionReader::load_auth_session;
    using AuthSessionWriter::save_auth_session;

    virtual ~SessionRepository() = default;

    virtual std::vector<AuthSession> list_auth_sessions() const {
        auto session = load_auth_session();
        if (!session.has_value()) {
            return {};
        }
        return {*session};
    }

    virtual std::optional<AuthSession> load_auth_session(std::string_view provider,
                                                         std::string_view subject_id) const {
        auto session = load_auth_session();
        if (!session.has_value() || !matches_session(*session, provider, subject_id)) {
            return std::nullopt;
        }
        return session;
    }

    virtual std::optional<AuthSessionKey> active_auth_session_key() const {
        auto session = load_auth_session();
        if (!session.has_value()) {
            return std::nullopt;
        }
        return session_key(*session);
    }

    virtual bool set_active_auth_session(std::string_view provider,
                                         std::string_view subject_id) {
        auto session = load_auth_session(provider, subject_id);
        if (!session.has_value()) {
            return false;
        }
        save_auth_session(*session);
        return true;
    }

    virtual void clear_auth_session(std::string_view provider,
                                    std::string_view subject_id) {
        const auto active = active_auth_session_key();
        if (active.has_value() && active->provider == provider &&
            active->subject_id == subject_id) {
            clear_auth_session();
        }
    }

    virtual void clear_all_auth_sessions() {
        clear_auth_session();
    }
};

} // namespace cauth::core::session

#endif
