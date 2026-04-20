#ifndef CAUTH_STEAM_AUTH_STEAM_AUTH_PROVIDER_HPP
#define CAUTH_STEAM_AUTH_STEAM_AUTH_PROVIDER_HPP

#include "core/session/session_repository.hpp"

#include <optional>
#include <string>

namespace cauth::steam::auth {

struct SteamAuthSessionLoadResult {
    bool ok = false;
    std::string error_message;
    std::optional<cauth::core::session::AuthSession> session;
};

class SteamAuthProvider {
  public:
    virtual ~SteamAuthProvider() = default;

    virtual SteamAuthSessionLoadResult load_auth_session() const = 0;
};

class StoredSteamAuthProvider final : public SteamAuthProvider {
  public:
    explicit StoredSteamAuthProvider(cauth::core::session::SessionRepository& repository);

    SteamAuthSessionLoadResult load_auth_session() const override;

  private:
    cauth::core::session::SessionRepository* repository_;
};

} // namespace cauth::steam::auth

#endif
