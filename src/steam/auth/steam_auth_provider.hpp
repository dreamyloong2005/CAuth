#ifndef CAUTH_STEAM_AUTH_STEAM_AUTH_PROVIDER_HPP
#define CAUTH_STEAM_AUTH_STEAM_AUTH_PROVIDER_HPP

#include "core/session/session_repository.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace cauth::steam::auth {

struct SteamAuthSessionLoadResult {
    bool ok = false;
    std::string error_message;
    std::optional<cauth::core::session::AuthSession> session;
};

enum class StoredSteamSessionSelection {
    SteamClientOnly,
    CloudAuto,
    WebApiPreferred,
};

std::optional<cauth::core::session::AuthSession> select_stored_steam_session(
    const cauth::core::session::SessionRepository& repository,
    std::string_view subject_id,
    StoredSteamSessionSelection selection);

class SteamAuthProvider {
  public:
    virtual ~SteamAuthProvider() = default;

    virtual SteamAuthSessionLoadResult load_auth_session(std::string_view subject_id) const = 0;
};

class StoredSteamAuthProvider final : public SteamAuthProvider {
  public:
    explicit StoredSteamAuthProvider(cauth::core::session::SessionRepository& repository);

    SteamAuthSessionLoadResult load_auth_session(std::string_view subject_id) const override;

  private:
    cauth::core::session::SessionRepository* repository_;
};

} // namespace cauth::steam::auth

#endif
