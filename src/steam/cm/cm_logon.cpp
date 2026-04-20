#include "steam/cm/cm_logon.hpp"
#include "steam/auth/steam_session_identity.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace cauth::core::cm {
namespace {

constexpr std::uint64_t kSteamIdIndividualAccountZero = 76561197960265728ULL;
constexpr std::uint32_t kClientPackageVersion = 1771;

void append_varint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
        value >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

void append_tag(std::vector<std::uint8_t>& out, int field_number, int wire_type) {
    append_varint(out, static_cast<std::uint64_t>((field_number << 3) | wire_type));
}

void append_varint_field(std::vector<std::uint8_t>& out, int field_number, std::uint64_t value) {
    append_tag(out, field_number, 0);
    append_varint(out, value);
}

void append_fixed32_field(std::vector<std::uint8_t>& out, int field_number, std::uint32_t value) {
    append_tag(out, field_number, 5);
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_fixed64_field(std::vector<std::uint8_t>& out, int field_number, std::uint64_t value) {
    append_tag(out, field_number, 1);
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_string_field(std::vector<std::uint8_t>& out, int field_number, std::string_view value) {
    if (value.empty()) {
        return;
    }

    append_tag(out, field_number, 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void append_bytes_field(std::vector<std::uint8_t>& out, int field_number,
                        const std::vector<std::uint8_t>& value) {
    if (value.empty()) {
        return;
    }

    append_tag(out, field_number, 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void append_message_field(std::vector<std::uint8_t>& out, int field_number,
                          const std::vector<std::uint8_t>& value) {
    append_bytes_field(out, field_number, value);
}

std::uint32_t rotate_left(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

std::array<std::uint8_t, 20> sha1(std::string_view input) {
    std::vector<std::uint8_t> bytes{input.begin(), input.end()};
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;

    bytes.push_back(0x80);
    while ((bytes.size() % 64U) != 56U) {
        bytes.push_back(0);
    }

    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
    }

    std::uint32_t h0 = 0x67452301U;
    std::uint32_t h1 = 0xefcdab89U;
    std::uint32_t h2 = 0x98badcfeU;
    std::uint32_t h3 = 0x10325476U;
    std::uint32_t h4 = 0xc3d2e1f0U;

    for (std::size_t chunk = 0; chunk < bytes.size(); chunk += 64) {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const auto offset = chunk + index * 4;
            words[index] = (static_cast<std::uint32_t>(bytes[offset]) << 24) |
                           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
                           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
                           static_cast<std::uint32_t>(bytes[offset + 3]);
        }

        for (std::size_t index = 16; index < words.size(); ++index) {
            words[index] = rotate_left(words[index - 3] ^ words[index - 8] ^
                                           words[index - 14] ^ words[index - 16],
                                       1);
        }

        auto a = h0;
        auto b = h1;
        auto c = h2;
        auto d = h3;
        auto e = h4;

        for (std::size_t index = 0; index < words.size(); ++index) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (index < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999U;
            } else if (index < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1U;
            } else if (index < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdcU;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6U;
            }

            const auto temp = rotate_left(a, 5) + f + e + k + words[index];
            e = d;
            d = c;
            c = rotate_left(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<std::uint8_t, 20> digest{};
    const std::array<std::uint32_t, 5> words{h0, h1, h2, h3, h4};
    for (std::size_t index = 0; index < words.size(); ++index) {
        digest[index * 4] = static_cast<std::uint8_t>((words[index] >> 24) & 0xffU);
        digest[index * 4 + 1] = static_cast<std::uint8_t>((words[index] >> 16) & 0xffU);
        digest[index * 4 + 2] = static_cast<std::uint8_t>((words[index] >> 8) & 0xffU);
        digest[index * 4 + 3] = static_cast<std::uint8_t>(words[index] & 0xffU);
    }
    return digest;
}

std::string hex_digest(const std::array<std::uint8_t, 20>& digest) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : digest) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

void append_binary_kv_string(std::vector<std::uint8_t>& out, std::string_view name,
                             std::string_view value) {
    out.push_back(1);
    out.insert(out.end(), name.begin(), name.end());
    out.push_back(0);
    out.insert(out.end(), value.begin(), value.end());
    out.push_back(0);
}

std::vector<std::uint8_t> make_machine_id(std::string_view account_name) {
    std::vector<std::uint8_t> out;
    const auto bb3 = hex_digest(sha1(std::string{"SteamUser Hash BB3 "} + std::string{account_name}));
    const auto ff2 = hex_digest(sha1(std::string{"SteamUser Hash FF2 "} + std::string{account_name}));
    const auto three_b3 =
        hex_digest(sha1(std::string{"SteamUser Hash 3B3 "} + std::string{account_name}));

    append_binary_kv_string(out, "BB3", bb3);
    append_binary_kv_string(out, "FF2", ff2);
    append_binary_kv_string(out, "3B3", three_b3);
    out.push_back(8);
    return out;
}

bool read_varint(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                 std::uint64_t& value) {
    value = 0;
    int shift = 0;
    while (offset < bytes.size() && shift <= 63) {
        const auto byte = bytes[offset++];
        value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            return true;
        }
        shift += 7;
    }
    return false;
}

bool skip_field(const std::vector<std::uint8_t>& bytes, std::size_t& offset, int wire_type) {
    std::uint64_t length = 0;
    switch (wire_type) {
    case 0:
        return read_varint(bytes, offset, length);
    case 1:
        if (bytes.size() - offset < 8) {
            return false;
        }
        offset += 8;
        return true;
    case 2:
        if (!read_varint(bytes, offset, length) || bytes.size() - offset < length) {
            return false;
        }
        offset += static_cast<std::size_t>(length);
        return true;
    case 5:
        if (bytes.size() - offset < 4) {
            return false;
        }
        offset += 4;
        return true;
    default:
        return false;
    }
}

void append_diagnostic(CmLogonResponse& response, int field_number, int wire_type,
                       std::string_view value) {
    std::ostringstream out;
    out << "field=" << field_number << " wire=" << wire_type;
    if (!value.empty()) {
        out << " value=" << value;
    }
    response.diagnostic_fields.push_back(out.str());
}

std::string uint_to_string(std::uint64_t value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

} // namespace

std::vector<std::uint8_t> encode_client_logon_body(const session::AuthSession& session,
                                                   const CmLogonRequest& request) {
    std::vector<std::uint8_t> out;
    append_varint_field(out, 1, request.protocol_version);
    append_varint_field(out, 2, request.obfuscated_private_ip);
    append_varint_field(out, 3, request.cell_id);
    append_varint_field(out, 5, kClientPackageVersion);
    append_string_field(out, 6, "english");
    append_varint_field(out, 7, 16);
    append_varint_field(out, 8, 1);
    std::vector<std::uint8_t> obfuscated_ip;
    append_fixed32_field(obfuscated_ip, 1, request.obfuscated_private_ip);
    append_message_field(out, 11, obfuscated_ip);
    append_bytes_field(out, 30, make_machine_id(session.account_name));
    append_string_field(out, 50, session.account_name);
    append_string_field(out, 96, request.machine_name);
    append_string_field(out, 97, request.machine_name);
    append_varint_field(out, 102, 1);
    append_string_field(out, 108, session.refresh_token);
    return out;
}

CmMessage make_client_logon_message(const session::AuthSession& session,
                                    const CmLogonRequest& request) {
    std::vector<std::uint8_t> header;
    append_fixed64_field(header, 1, kSteamIdIndividualAccountZero);

    return CmMessage{
        EMsg::ClientLogon,
        true,
        std::move(header),
        encode_client_logon_body(session, request),
    };
}

std::optional<CmLogonResponse> parse_client_logon_response_body(
    const std::vector<std::uint8_t>& bytes) {
    CmLogonResponse response;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(bytes, offset, tag)) {
            return std::nullopt;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);

        if (field_number == 1 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            response.eresult = static_cast<std::uint32_t>(value);
            response.ok = response.eresult == 1;
            append_diagnostic(response, field_number, wire_type, uint_to_string(value));
            continue;
        }

        if ((field_number == 2 || field_number == 3) && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            if (field_number == 2) {
                response.legacy_out_of_game_heartbeat_seconds = static_cast<std::uint32_t>(value);
                if (response.heartbeat_seconds == 0) {
                    response.heartbeat_seconds = static_cast<std::uint32_t>(value);
                }
            } else {
                response.heartbeat_seconds = static_cast<std::uint32_t>(value);
            }
            append_diagnostic(response, field_number, wire_type, uint_to_string(value));
            continue;
        }

        if ((field_number == 7 || field_number == 10 || field_number == 24 ||
             field_number == 25 || field_number == 26 || field_number == 27 ||
             field_number == 28 || field_number == 30) &&
            wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            if (field_number == 7) {
                response.cell_id = static_cast<std::uint32_t>(value);
            } else if (field_number == 10) {
                response.eresult_extended = static_cast<std::uint32_t>(value);
            } else if (field_number == 24) {
                response.count_loginfailures_to_migrate = static_cast<std::uint32_t>(value);
            } else if (field_number == 25) {
                response.count_disconnects_to_migrate = static_cast<std::uint32_t>(value);
            } else if (field_number == 26) {
                response.ogs_data_report_time_window = static_cast<std::uint32_t>(value);
            } else if (field_number == 27) {
                response.client_instance_id = value;
            } else if (field_number == 28) {
                response.force_client_update_check = value != 0;
            } else {
                response.token_id = value;
            }
            append_diagnostic(response, field_number, wire_type, uint_to_string(value));
            continue;
        }

        if (wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            append_diagnostic(response, field_number, wire_type, uint_to_string(value));
            continue;
        }

        append_diagnostic(response, field_number, wire_type, {});
        if (!skip_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return response;
}

} // namespace cauth::core::cm
