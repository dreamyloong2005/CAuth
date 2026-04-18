#include "steam/auth/steam_auth_provider.hpp"
#include "steam/auth/steam_session_identity.hpp"

namespace cauth::steam::auth {

StoredSteamAuthProvider::StoredSteamAuthProvider(cauth::core::session::AuthSessionReader& reader)
    : reader_(&reader) {}

SteamAuthSessionLoadResult StoredSteamAuthProvider::load_auth_session() const {
    if (reader_ == nullptr) {
        return {false, "Steam auth repository is not configured", std::nullopt};
    }

    const auto session = reader_->load_auth_session();
    if (!session.has_value()) {
        return {false, "Auth session: not signed in", std::nullopt};
    }

    if (!cauth::core::session::is_valid(*session)) {
        return {false, "Auth session: invalid or incomplete", std::nullopt};
    }

    if (cauth::steam::auth::steam_id(*session) == 0) {
        return {false, "Auth session: Steam subject is missing", std::nullopt};
    }

    return {true, "", session};
}

} // namespace cauth::steam::auth
