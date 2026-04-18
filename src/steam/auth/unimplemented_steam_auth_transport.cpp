#include "steam/auth/unimplemented_steam_auth_transport.hpp"

namespace cauth::steam::auth {
namespace {

template <typename T> SteamTransportResponse<T> unimplemented_response() {
    return SteamTransportResponse<T>{
        SteamTransportResult{false, "Steam authentication transport is not implemented yet"},
        std::nullopt,
    };
}

} // namespace

SteamTransportResponse<SteamRsaPublicKey>
UnimplementedSteamAuthenticationTransport::get_password_rsa_public_key(const std::string&) {
    return unimplemented_response<SteamRsaPublicKey>();
}

SteamTransportResponse<SteamBeginAuthSessionResponse>
UnimplementedSteamAuthenticationTransport::begin_auth_session_via_credentials(
    const SteamBeginAuthSessionRequest&) {
    return unimplemented_response<SteamBeginAuthSessionResponse>();
}

SteamTransportResponse<SteamPollAuthSessionStatusResponse>
UnimplementedSteamAuthenticationTransport::poll_auth_session_status(
    const SteamPollAuthSessionStatusRequest&) {
    return unimplemented_response<SteamPollAuthSessionStatusResponse>();
}

SteamTransportResponse<SteamGenerateAccessTokenForAppResponse>
UnimplementedSteamAuthenticationTransport::generate_access_token_for_app(
    const SteamGenerateAccessTokenForAppRequest&) {
    return unimplemented_response<SteamGenerateAccessTokenForAppResponse>();
}

} // namespace cauth::steam::auth
