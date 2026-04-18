#ifndef CAUTH_CORE_CM_CM_SERVICE_METHOD_HPP
#define CAUTH_CORE_CM_CM_SERVICE_METHOD_HPP

#include "steam/cm/cm_message.hpp"
#include "steam/cm/websocket_transport.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::core::cm {

struct CmServiceMethodResponseHeader {
    std::uint64_t job_id_target = 0;
    std::uint32_t eresult = 0;
    std::string error_message;
};

struct CmServiceMethodCallResult {
    bool ok = false;
    std::string error_message;
    CmServiceMethodResponseHeader header;
    std::vector<std::uint8_t> body;
};

CmMessage make_non_authed_service_method_call(std::string_view target_job_name,
                                              const std::vector<std::uint8_t>& body,
                                              std::uint64_t job_id_source);
CmMessage make_service_method_call(std::string_view target_job_name,
                                   const std::vector<std::uint8_t>& body,
                                   std::uint64_t job_id_source);

std::optional<CmServiceMethodResponseHeader>
parse_service_method_response_header(const std::vector<std::uint8_t>& header);

class CmServiceMethodClient {
  public:
    explicit CmServiceMethodClient(CmWebSocketConnection& connection);

    CmServiceMethodCallResult call_non_authed(std::string_view target_job_name,
                                              const std::vector<std::uint8_t>& body,
                                              std::uint64_t job_id_source,
                                              int max_receive_attempts = 8);
    CmServiceMethodCallResult call(std::string_view target_job_name,
                                   const std::vector<std::uint8_t>& body,
                                   std::uint64_t job_id_source,
                                   int max_receive_attempts = 8);
    CmServiceMethodCallResult call_with_message(const CmMessage& message,
                                                std::uint64_t job_id_source,
                                                int max_receive_attempts);

  private:
    CmWebSocketConnection& connection_;
};

} // namespace cauth::core::cm

#endif
