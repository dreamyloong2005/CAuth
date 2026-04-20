#ifndef CAUTH_CORE_RUNTIME_WINDOWS_WINDOWS_SESSION_REPOSITORY_HPP
#define CAUTH_CORE_RUNTIME_WINDOWS_WINDOWS_SESSION_REPOSITORY_HPP

#include "core/session/session_repository.hpp"

#include <filesystem>

namespace cauth::core::runtime {

class WindowsSessionRepository final : public session::SessionRepository {
  public:
    WindowsSessionRepository();
    explicit WindowsSessionRepository(std::filesystem::path path);

    void save_auth_session(const session::AuthSession& session) override;
    std::optional<session::AuthSession> load_auth_session() const override;
    void clear_auth_session() override;
    std::vector<session::AuthSession> list_auth_sessions() const override;
    std::optional<session::AuthSession> load_auth_session(std::string_view provider,
                                                          std::string_view subject_id) const override;
    std::optional<session::AuthSessionKey> active_auth_session_key() const override;
    bool set_active_auth_session(std::string_view provider, std::string_view subject_id) override;
    void clear_auth_session(std::string_view provider, std::string_view subject_id) override;
    void clear_all_auth_sessions() override;

    const std::filesystem::path& path() const noexcept;

  private:
    session::AuthSessionRepositoryState load_repository_state() const;
    void save_repository_state(const session::AuthSessionRepositoryState& state);

    std::filesystem::path path_;
};

} // namespace cauth::core::runtime

#endif
