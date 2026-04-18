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

    const std::filesystem::path& path() const noexcept;

  private:
    std::filesystem::path path_;
};

} // namespace cauth::core::runtime

#endif
