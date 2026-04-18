#include "core/session/auth_session.hpp"
#include "steam/auth/steam_session_identity.hpp"
#include "steam/cm/cm_heartbeat.hpp"
#include "steam/cm/cm_message.hpp"

#include <iostream>

int main() {
    const cauth::core::session::AuthSession session{
        std::string{cauth::steam::auth::kSteamAuthProvider},
        "76561198000000000",
        "test_account",
        "refresh-token",
        "access-token",
    };

    const auto heartbeat = cauth::core::cm::make_client_heartbeat_message(session);
    if (heartbeat.emsg != cauth::core::cm::EMsg::ClientHeartBeat || !heartbeat.protobuf ||
        heartbeat.header.empty() || !heartbeat.body.empty()) {
        std::cerr << "heartbeat should be a protobuf CM message with header and empty body\n";
        return 1;
    }

    const auto encoded = cauth::core::cm::encode_cm_message(heartbeat);
    const auto decoded = cauth::core::cm::decode_cm_message(encoded);
    if (!decoded.has_value() || decoded->emsg != cauth::core::cm::EMsg::ClientHeartBeat ||
        decoded->header != heartbeat.header || !decoded->body.empty()) {
        std::cerr << "heartbeat should round-trip through CM framing\n";
        return 1;
    }

    return 0;
}
