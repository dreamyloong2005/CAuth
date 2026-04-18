#ifndef CAUTH_CORE_CM_CM_MESSAGE_HPP
#define CAUTH_CORE_CM_CM_MESSAGE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cauth::core::cm {

constexpr std::uint32_t kProtoMask = 0x80000000U;

enum class EMsg : std::uint32_t {
    Invalid = 0,
    Multi = 1,
    ServiceMethodResponse = 147,
    ServiceMethodCallFromClient = 151,
    ServiceMethodSendToClient = 152,
    ClientLogOnResponse = 751,
    ClientLogOff = 706,
    ClientHeartBeat = 703,
    ClientSessionToken = 850,
    ClientGetDepotDecryptionKey = 5438,
    ClientGetDepotDecryptionKeyResponse = 5439,
    ClientServersAvailable = 5501,
    ClientLogon = 5514,
    ClientPICSProductInfoRequest = 8903,
    ClientPICSProductInfoResponse = 8904,
    ClientPICSAccessTokenRequest = 8905,
    ClientPICSAccessTokenResponse = 8906,
    ServiceMethodCallFromClientNonAuthed = 9804,
    ClientHello = 9805,
};

struct CmMessage {
    EMsg emsg = EMsg::Invalid;
    bool protobuf = true;
    std::vector<std::uint8_t> header;
    std::vector<std::uint8_t> body;
};

std::uint32_t raw_emsg(EMsg emsg, bool protobuf);
EMsg clean_emsg(std::uint32_t raw);
bool is_proto_emsg(std::uint32_t raw);
const char* emsg_name(EMsg emsg);
std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes);

std::vector<std::uint8_t> encode_cm_message(const CmMessage& message);
std::optional<CmMessage> decode_cm_message(const std::vector<std::uint8_t>& bytes);
std::vector<CmMessage> unpack_cm_messages(const CmMessage& message, std::string* error_message);

} // namespace cauth::core::cm

#endif
