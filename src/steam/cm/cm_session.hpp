#ifndef CAUTH_CORE_CM_CM_SESSION_HPP
#define CAUTH_CORE_CM_CM_SESSION_HPP

#include "core/session/auth_session.hpp"
#include "steam/cm/cm_logon.hpp"
#include "steam/cm/cm_server.hpp"
#include "steam/cm/cm_service_method.hpp"
#include "steam/cm/websocket_transport.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::core::cm {

struct CmSessionConnectResult {
    bool ok = false;
    std::string error_message;
    CmLogonResponse logon_response;
};

struct CmSessionReceiveResult {
    bool ok = false;
    std::string error_message;
    std::vector<CmMessage> messages;
};

class CmSession {
  public:
    explicit CmSession(std::unique_ptr<CmWebSocketConnection> connection);
    ~CmSession();

    CmSession(const CmSession&) = delete;
    CmSession& operator=(const CmSession&) = delete;
    CmSession(CmSession&&) noexcept = default;
    CmSession& operator=(CmSession&&) noexcept = default;

    CmSessionConnectResult logon(const session::AuthSession& session,
                                 const CmLogonRequest& request = {},
                                 int max_receive_attempts = 8);
    CmServiceMethodCallResult call_service_method(std::string_view target_job_name,
                                                  const std::vector<std::uint8_t>& body,
                                                  std::uint64_t job_id_source,
                                                  int max_receive_attempts = 8);
    CmWebSocketProbeResult send_heartbeat(const session::AuthSession& session);
    CmWebSocketProbeResult send_message(const CmMessage& message);
    CmSessionReceiveResult receive_messages(int max_receive_attempts = 8);
    void close();

  private:
    std::unique_ptr<CmWebSocketConnection> connection_;
    std::uint64_t steam_id_ = 0;
    std::uint32_t client_session_id_ = 0;
};

} // namespace cauth::core::cm

#endif
