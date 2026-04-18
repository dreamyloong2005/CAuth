#include "cauth/core.hpp"
#include "cauth/steam_auth.hpp"
#include "cauth/steam_depot.hpp"
#include "cauth/steam_cloud.hpp"

#include <iostream>

int main() {
    cauth::core::session::AuthSession session;
    session.provider = std::string{cauth::steam::auth::kSteamAuthProvider};
    session.subject_id = "76561198000000000";
    session.account_name = "test_account";
    session.refresh_token = "refresh-token";

    const cauth::core::auth::AuthResult auth_result{
        cauth::core::auth::AuthStatus::Succeeded,
        cauth::core::auth::AuthChallengeKind::None,
        session,
        session.provider,
        "ok",
    };

    const auto encoded = cauth::core::session::encode_auth_session(session);
    const auto decoded = cauth::core::session::decode_auth_session(encoded);
    if (!decoded.has_value() || cauth::steam::auth::steam_id(*decoded) == 0 ||
        auth_result.provider != std::string{cauth::steam::auth::kSteamAuthProvider}) {
        std::cerr << "public module umbrella headers should expose usable auth/session types\n";
        return 1;
    }

    cauth::core::depot::DepotBranchSelection selection;
    selection.branch = "public";
    if (selection.branch != "public") {
        std::cerr << "public steam depot header should expose depot types\n";
        return 1;
    }

    cauth::steam::cloud::SteamCloudRequest sync_request;
    sync_request.app_id = 440;
    if (sync_request.app_id != 440) {
        std::cerr << "public steam cloud header should expose sync types\n";
        return 1;
    }

    return 0;
}
