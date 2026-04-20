#ifndef CAUTH_CORE_RUNTIME_SESSION_MEMORY_SESSION_REPOSITORY_HPP
#define CAUTH_CORE_RUNTIME_SESSION_MEMORY_SESSION_REPOSITORY_HPP

#include "core/session/session_repository.hpp"

namespace cauth::core::runtime {

class MemorySessionRepository final : public session::SessionRepository {
  public:
    void save_auth_session(const session::AuthSession& session) override;
    std::vector<session::AuthSession> list_auth_sessions() const override;
    std::optional<session::AuthSession> load_auth_session(std::string_view provider,
                                                          std::string_view subject_id) const override;
    void clear_auth_session(std::string_view provider, std::string_view subject_id) override;
    void clear_all_auth_sessions() override;

  private:
    session::AuthSessionRepositoryState state_;
};

} // namespace cauth::core::runtime

#endif
