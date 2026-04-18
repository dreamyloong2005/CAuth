#ifndef CAUTH_CORE_CM_CM_LOGON_HPP
#define CAUTH_CORE_CM_CM_LOGON_HPP

#include "core/session/auth_session.hpp"
#include "steam/cm/cm_message.hpp"
#include "steam/cm/cm_server.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cauth::core::cm {

struct CmLogonRequest {
    std::uint32_t protocol_version = 65580;
    std::uint32_t cell_id = 0;
    std::uint32_t obfuscated_private_ip = 0xbaadf00d;
    std::string machine_name = "CAuth";
};

struct CmLogonResponse {
    bool ok = false;
    std::uint32_t eresult = 0;
    std::uint32_t eresult_extended = 0;
    std::uint32_t heartbeat_seconds = 0;
    std::uint32_t legacy_out_of_game_heartbeat_seconds = 0;
    std::uint32_t cell_id = 0;
    std::uint32_t count_loginfailures_to_migrate = 0;
    std::uint32_t count_disconnects_to_migrate = 0;
    std::uint32_t ogs_data_report_time_window = 0;
    bool force_client_update_check = false;
    std::uint64_t client_instance_id = 0;
    std::uint64_t token_id = 0;
    std::string error_message;
    std::vector<std::string> diagnostic_fields;
};

std::vector<std::uint8_t> encode_client_logon_body(const session::AuthSession& session,
                                                   const CmLogonRequest& request);
CmMessage make_client_logon_message(const session::AuthSession& session,
                                    const CmLogonRequest& request);
std::optional<CmLogonResponse> parse_client_logon_response_body(
    const std::vector<std::uint8_t>& bytes);

} // namespace cauth::core::cm

#endif
