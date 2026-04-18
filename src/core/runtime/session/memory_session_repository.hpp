#ifndef CAUTH_CORE_RUNTIME_SESSION_MEMORY_SESSION_REPOSITORY_HPP
#define CAUTH_CORE_RUNTIME_SESSION_MEMORY_SESSION_REPOSITORY_HPP

#include "core/session/session_repository.hpp"

namespace cauth::core::runtime {

class MemorySessionRepository final : public session::SessionRepository {
  public:
    void save_auth_session(const session::AuthSession& session) override;
    std::optional<session::AuthSession> load_auth_session() const override;
    void clear_auth_session() override;

  private:
    std::optional<session::AuthSession> session_;
};

} // namespace cauth::core::runtime

#endif
