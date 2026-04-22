#ifndef CAUTH_CORE_RUNTIME_SESSION_ANDROID_SESSION_REPOSITORY_HPP
#define CAUTH_CORE_RUNTIME_SESSION_ANDROID_SESSION_REPOSITORY_HPP

#include "core/session/session_repository.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace cauth::core::runtime {

class AndroidSecureStorageBridge {
  public:
    virtual ~AndroidSecureStorageBridge() = default;

    virtual void save_bytes(std::vector<std::uint8_t> bytes) = 0;
    virtual std::optional<std::vector<std::uint8_t>> load_bytes() const = 0;
    virtual void clear_bytes() = 0;
};

class AndroidSessionRepository final : public session::SessionRepository {
  public:
    explicit AndroidSessionRepository(AndroidSecureStorageBridge& bridge);
    explicit AndroidSessionRepository(std::unique_ptr<AndroidSecureStorageBridge> bridge);

    void save_auth_session(const session::AuthSession& session) override;
    std::vector<session::AuthSession> list_auth_sessions() const override;
    std::optional<session::AuthSession> load_auth_session(std::string_view provider,
                                                          std::string_view subject_id) const override;
    void clear_auth_session(std::string_view provider, std::string_view subject_id) override;
    void clear_all_auth_sessions() override;

  private:
    session::AuthSessionRepositoryState load_repository_state() const;
    void save_repository_state(const session::AuthSessionRepositoryState& state);

    std::unique_ptr<AndroidSecureStorageBridge> owned_bridge_;
    AndroidSecureStorageBridge* bridge_;
};

} // namespace cauth::core::runtime

#endif
