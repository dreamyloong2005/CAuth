#include "steam/auth/steam_auth_provider.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <optional>

namespace cauth::steam::auth {
namespace {

void keep_newest(std::optional<cauth::core::session::AuthSession>& selected,
                 const cauth::core::session::AuthSession& candidate) {
    if (!selected.has_value() || candidate.created_at >= selected->created_at) {
        selected = candidate;
    }
}

std::optional<cauth::core::session::AuthSession> select_steam_client_session(
    const cauth::core::session::SessionRepository& repository,
    const cauth::core::session::AuthSessionKey& active) {
    std::optional<cauth::core::session::AuthSession> client;
    std::optional<cauth::core::session::AuthSession> legacy;

    for (const auto& session : repository.list_auth_sessions()) {
        if (!cauth::core::session::matches_session(session, active) ||
            !cauth::core::session::is_valid(session) ||
            steam_id(session) == 0) {
            continue;
        }
        if (session.session_type == kSteamSessionTypeSteamClient) {
            keep_newest(client, session);
            continue;
        }
        if (session.session_type.empty()) {
            keep_newest(legacy, session);
        }
    }

    return client.has_value() ? client : legacy;
}

} // namespace

StoredSteamAuthProvider::StoredSteamAuthProvider(
    cauth::core::session::SessionRepository& repository)
    : repository_(&repository) {}

SteamAuthSessionLoadResult StoredSteamAuthProvider::load_auth_session() const {
    if (repository_ == nullptr) {
        return {false, "Steam auth repository is not configured", std::nullopt};
    }

    const auto active = repository_->active_auth_session_key();
    if (!active.has_value()) {
        return {false, "Auth session: not signed in", std::nullopt};
    }
    if (active->provider != kSteamAuthProvider) {
        return {false, "Auth session: active account is not a Steam account", std::nullopt};
    }

    const auto session = select_steam_client_session(*repository_, *active);
    if (!session.has_value()) {
        return {false,
                "Auth session: Steam client login is required; run steam auth login",
                std::nullopt};
    }

    return {true, "", session};
}

} // namespace cauth::steam::auth
