#include "steam/cloud/steam_cloud_cm_service.hpp"

#include "core/platform/http_client.hpp"
#include "core/session/auth_session.hpp"
#include "core/transfer/transfer_resume.hpp"
#include "steam/auth/steam_session_identity.hpp"
#include "steam/cm/steam_cm_connector.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(CAUTH_HAS_ZLIB)
#include <zlib.h>
#endif

namespace cauth::steam::cloud {
namespace {

using cauth::core::cm::SteamCmAttemptResult;
using cauth::core::cm::SteamCmConnector;
using cauth::core::cm::SteamCmContinuation;

constexpr std::uint64_t kEnumerateUserFilesJobId = 0x4341555448535941ULL;
constexpr std::uint64_t kClientFileDownloadJobId = 0x4341555448535942ULL;
constexpr std::uint64_t kBeginAppUploadBatchJobId = 0x4341555448535943ULL;
constexpr std::uint64_t kClientBeginFileUploadJobId = 0x4341555448535944ULL;
constexpr std::uint64_t kClientCommitFileUploadJobId = 0x4341555448535945ULL;
constexpr std::uint64_t kCompleteAppUploadBatchJobId = 0x4341555448535946ULL;
constexpr std::string_view kCmUploadResumeFileName = ".cauthupload.resume";

void append_varint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 0x80U) {
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

void append_string_field(std::vector<std::uint8_t>& out, int field_number, std::string_view value) {
    if (value.empty()) {
        return;
    }
    append_tag(out, field_number, 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void append_bytes_field(std::vector<std::uint8_t>& out,
                        int field_number,
                        const std::vector<std::uint8_t>& value) {
    if (value.empty()) {
        return;
    }
    append_tag(out, field_number, 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

bool read_varint(const std::vector<std::uint8_t>& bytes,
                 std::size_t& offset,
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

bool read_fixed32(const std::vector<std::uint8_t>& bytes,
                  std::size_t& offset,
                  std::uint32_t& value) {
    if (bytes.size() - offset < 4) {
        return false;
    }
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    }
    return true;
}

bool read_fixed64(const std::vector<std::uint8_t>& bytes,
                  std::size_t& offset,
                  std::uint64_t& value) {
    if (bytes.size() - offset < 8) {
        return false;
    }
    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    }
    return true;
}

bool read_length_delimited(const std::vector<std::uint8_t>& bytes,
                           std::size_t& offset,
                           std::vector<std::uint8_t>& value) {
    std::uint64_t length = 0;
    if (!read_varint(bytes, offset, length) || bytes.size() - offset < length) {
        return false;
    }
    value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += static_cast<std::size_t>(length);
    return true;
}

bool read_length_delimited_string(const std::vector<std::uint8_t>& bytes,
                                  std::size_t& offset,
                                  std::string& value) {
    std::vector<std::uint8_t> raw;
    if (!read_length_delimited(bytes, offset, raw)) {
        return false;
    }
    value.assign(raw.begin(), raw.end());
    return true;
}

bool skip_field(const std::vector<std::uint8_t>& bytes, std::size_t& offset, int wire_type) {
    std::uint64_t ignored_varint = 0;
    std::uint32_t ignored32 = 0;
    std::uint64_t ignored64 = 0;
    std::vector<std::uint8_t> ignored_bytes;
    switch (wire_type) {
    case 0:
        return read_varint(bytes, offset, ignored_varint);
    case 1:
        return read_fixed64(bytes, offset, ignored64);
    case 2:
        return read_length_delimited(bytes, offset, ignored_bytes);
    case 5:
        return read_fixed32(bytes, offset, ignored32);
    default:
        return false;
    }
}

std::string join_strings(const std::vector<std::string>& values) {
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            result += ",";
        }
        result += values[index];
    }
    return result;
}

std::optional<cauth::core::session::AuthSession> make_cm_auth_session(
    const SteamCloudRequest& request) {
    if (request.refresh_token.empty()) {
        return std::nullopt;
    }

    cauth::core::session::AuthSession session;
    session.refresh_token = request.refresh_token;
    session.access_token = request.access_token;
    session.session_type = request.session_type.empty()
                               ? std::string{cauth::steam::auth::kSteamSessionTypeSteamClient}
                               : request.session_type;
    session.provider = std::string{cauth::steam::auth::kSteamAuthProvider};
    if (request.steam_id != 0) {
        session.subject_id = std::to_string(request.steam_id);
    }
    return session;
}

bool is_retryable_cm_error(std::string_view error) {
    return error.find("websocket") != std::string_view::npos ||
           error.find("timed out") != std::string_view::npos ||
           error.find("receive failed") != std::string_view::npos;
}

std::optional<std::vector<std::uint8_t>> decode_hex_bytes(std::string_view hex) {
    if (hex.empty() || (hex.size() % 2) != 0) {
        return std::nullopt;
    }

    auto decode_nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return 10 + (ch - 'a');
        }
        if (ch >= 'A' && ch <= 'F') {
            return 10 + (ch - 'A');
        }
        return -1;
    };

    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t index = 0; index < hex.size(); index += 2) {
        const auto high = decode_nibble(hex[index]);
        const auto low = decode_nibble(hex[index + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return bytes;
}

std::uint64_t unix_time_now_seconds() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

void append_le16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
}

void append_le32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
}

std::optional<std::uint64_t> file_timestamp_seconds(std::string_view path) {
    if (path.empty()) {
        return std::nullopt;
    }

    std::error_code ec;
    const auto value = std::filesystem::last_write_time(std::filesystem::path{path}, ec);
    if (ec) {
        return std::nullopt;
    }

    using namespace std::chrono;
    const auto system_now = system_clock::now();
    const auto file_now = std::filesystem::file_time_type::clock::now();
    const auto system_time =
        time_point_cast<system_clock::duration>(value - file_now + system_now);
    const auto seconds = duration_cast<std::chrono::seconds>(system_time.time_since_epoch()).count();
    if (seconds < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(seconds);
}

struct PreparedUploadFile {
    const SteamCloudUploadFile* source = nullptr;
    std::vector<std::uint8_t> upload_bytes;
    std::uint32_t raw_file_size = 0;
    std::uint64_t timestamp = 0;
    bool compressed = false;
};

using CmUploadResumeState = cauth::core::transfer::TransferResumeState;

enum class UploadInterruptAction {
    None,
    Pause,
    Cancel,
};

std::filesystem::path make_cm_upload_resume_state_path(std::string_view local_root) {
    return std::filesystem::path{std::string{local_root}} /
           std::filesystem::path{std::string{kCmUploadResumeFileName}};
}

std::string make_cm_upload_file_token(const SteamCloudRequest& request,
                                      const PreparedUploadFile& file) {
    return std::string{"steam-cloud-push-cm-file-v1:"} + std::to_string(request.app_id) + ":" +
           std::to_string(request.steam_id) + ":" + file.source->filename + ":" +
           std::to_string(file.raw_file_size) + ":" + file.source->file_sha + ":" +
           (file.compressed ? "zip" : "raw") + ":" + std::to_string(file.upload_bytes.size());
}

std::string make_cm_upload_batch_token(const SteamCloudRequest& request,
                                       std::string_view machine_name,
                                       const std::vector<PreparedUploadFile>& files,
                                       const std::vector<std::string>& files_to_delete) {
    std::string token = std::string{"steam-cloud-push-cm-batch-v1:"} +
                        std::to_string(request.app_id) + ":" +
                        std::to_string(request.steam_id) + ":" +
                        std::string{machine_name};
    for (const auto& file : files) {
        token.push_back('|');
        token += make_cm_upload_file_token(request, file);
    }
    token += "|delete:";
    for (const auto& filename : files_to_delete) {
        token += filename;
        token.push_back(';');
    }
    return token;
}

bool load_cm_upload_resume_state(const std::filesystem::path& path,
                                 CmUploadResumeState& state,
                                 std::string& error_message) {
    if (!cauth::core::transfer::load_transfer_resume_state(path, state, error_message)) {
        return false;
    }
    if (state.group_id == 0 || state.item_token.empty()) {
        error_message = "Cloud upload resume state is incomplete: " + path.string();
        return false;
    }
    return true;
}

bool save_cm_upload_resume_state(const std::filesystem::path& path,
                                 const CmUploadResumeState& state,
                                 std::string& error_message) {
    return cauth::core::transfer::save_transfer_resume_state(path, state, error_message);
}

bool clear_cm_upload_resume_state(const std::filesystem::path& path, std::string& error_message) {
    return cauth::core::transfer::clear_transfer_resume_state(path, error_message);
}

UploadInterruptAction current_upload_interrupt_action(const SteamCloudUploadCallbacks& callbacks) {
    const auto pause_requested =
        callbacks.pause_hook != nullptr && callbacks.pause_hook(callbacks.user_data);
    const auto cancel_requested =
        callbacks.cancel_hook != nullptr && callbacks.cancel_hook(callbacks.user_data);
    if (pause_requested) {
        return UploadInterruptAction::Pause;
    }
    if (cancel_requested) {
        return UploadInterruptAction::Cancel;
    }
    return UploadInterruptAction::None;
}

#if defined(CAUTH_HAS_ZLIB)
std::optional<std::vector<std::uint8_t>> raw_deflate_bytes(const std::vector<std::uint8_t>& input) {
    z_stream stream{};
    if (deflateInit2(
            &stream,
            Z_DEFAULT_COMPRESSION,
            Z_DEFLATED,
            -MAX_WBITS,
            8,
            Z_DEFAULT_STRATEGY) != Z_OK) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> output;
    output.resize(compressBound(static_cast<uLong>(input.size())));
    stream.next_in = const_cast<Bytef*>(
        reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    const auto result = deflate(&stream, Z_FINISH);
    deflateEnd(&stream);
    if (result != Z_STREAM_END) {
        return std::nullopt;
    }
    output.resize(stream.total_out);
    return output;
}

std::optional<std::vector<std::uint8_t>> make_single_entry_zip(
    const std::vector<std::uint8_t>& raw_bytes) {
    static constexpr std::string_view kEntryName = "data";
    const auto deflated = raw_deflate_bytes(raw_bytes);
    if (!deflated.has_value()) {
        return std::nullopt;
    }

    const auto crc = static_cast<std::uint32_t>(
        crc32(0L, reinterpret_cast<const Bytef*>(raw_bytes.data()), static_cast<uInt>(raw_bytes.size())));

    std::vector<std::uint8_t> zip;
    zip.reserve(deflated->size() + 128);

    append_le32(zip, 0x04034b50U);
    append_le16(zip, 20);
    append_le16(zip, 0);
    append_le16(zip, 8);
    append_le16(zip, 0);
    append_le16(zip, 0);
    append_le32(zip, crc);
    append_le32(zip, static_cast<std::uint32_t>(deflated->size()));
    append_le32(zip, static_cast<std::uint32_t>(raw_bytes.size()));
    append_le16(zip, static_cast<std::uint16_t>(kEntryName.size()));
    append_le16(zip, 0);
    zip.insert(zip.end(), kEntryName.begin(), kEntryName.end());
    zip.insert(zip.end(), deflated->begin(), deflated->end());

    const auto central_directory_offset = static_cast<std::uint32_t>(zip.size());
    append_le32(zip, 0x02014b50U);
    append_le16(zip, 20);
    append_le16(zip, 20);
    append_le16(zip, 0);
    append_le16(zip, 8);
    append_le16(zip, 0);
    append_le16(zip, 0);
    append_le32(zip, crc);
    append_le32(zip, static_cast<std::uint32_t>(deflated->size()));
    append_le32(zip, static_cast<std::uint32_t>(raw_bytes.size()));
    append_le16(zip, static_cast<std::uint16_t>(kEntryName.size()));
    append_le16(zip, 0);
    append_le16(zip, 0);
    append_le16(zip, 0);
    append_le16(zip, 0);
    append_le32(zip, 0);
    append_le32(zip, 0);
    zip.insert(zip.end(), kEntryName.begin(), kEntryName.end());

    const auto central_directory_size =
        static_cast<std::uint32_t>(zip.size()) - central_directory_offset;
    append_le32(zip, 0x06054b50U);
    append_le16(zip, 0);
    append_le16(zip, 0);
    append_le16(zip, 1);
    append_le16(zip, 1);
    append_le32(zip, central_directory_size);
    append_le32(zip, central_directory_offset);
    append_le16(zip, 0);
    return zip;
}
#endif

PreparedUploadFile prepare_upload_file(const SteamCloudUploadFile& file) {
    PreparedUploadFile prepared;
    prepared.source = &file;
    prepared.upload_bytes = file.bytes;
    prepared.raw_file_size = static_cast<std::uint32_t>(file.bytes.size());
    prepared.timestamp = file_timestamp_seconds(file.local_path).value_or(unix_time_now_seconds());

#if defined(CAUTH_HAS_ZLIB)
    const auto zipped = make_single_entry_zip(file.bytes);
    if (zipped.has_value() && zipped->size() < file.bytes.size()) {
        prepared.upload_bytes = *zipped;
        prepared.compressed = true;
    }
#endif

    return prepared;
}

std::vector<std::uint8_t> build_enumerate_user_files_request(std::uint32_t app_id,
                                                             std::uint32_t count,
                                                             std::uint32_t start_index,
                                                             bool extended_details) {
    std::vector<std::uint8_t> body;
    append_varint_field(body, 1, app_id);
    append_varint_field(body, 2, extended_details ? 1U : 0U);
    append_varint_field(body, 3, count);
    append_varint_field(body, 4, start_index);
    return body;
}

std::vector<std::uint8_t> build_client_file_download_request(std::uint32_t app_id,
                                                             std::string_view filename) {
    std::vector<std::uint8_t> body;
    append_varint_field(body, 1, app_id);
    append_string_field(body, 2, filename);
    return body;
}

std::vector<std::uint8_t> build_begin_app_upload_batch_request(
    std::uint32_t app_id,
    std::string_view machine_name,
    const std::vector<std::string>& files_to_upload,
    const std::vector<std::string>& files_to_delete) {
    std::vector<std::uint8_t> body;
    append_varint_field(body, 1, app_id);
    append_string_field(body, 2, machine_name);
    for (const auto& filename : files_to_upload) {
        append_string_field(body, 3, filename);
    }
    for (const auto& filename : files_to_delete) {
        append_string_field(body, 4, filename);
    }
    return body;
}

std::optional<std::vector<std::uint8_t>> build_client_begin_file_upload_request(
    std::uint32_t app_id,
    std::uint64_t batch_id,
    const PreparedUploadFile& file) {
    if (file.upload_bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    const auto file_sha = decode_hex_bytes(file.source->file_sha);
    if (!file_sha.has_value()) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> body;
    append_varint_field(body, 1, app_id);
    append_varint_field(body, 2, static_cast<std::uint32_t>(file.upload_bytes.size()));
    append_varint_field(body, 3, file.raw_file_size);
    append_bytes_field(body, 4, *file_sha);
    append_varint_field(body, 5, file.timestamp);
    append_string_field(body, 6, file.source->filename);
    append_varint_field(body, 7, 0xffffffffULL);
    append_varint_field(body, 10, 0);
    append_varint_field(body, 11, 0);
    if (batch_id != 0) {
        append_varint_field(body, 13, batch_id);
    }
    return body;
}

std::optional<std::vector<std::uint8_t>> build_client_commit_file_upload_request(
    std::uint32_t app_id,
    const PreparedUploadFile& file,
    bool transfer_succeeded) {
    const auto file_sha = decode_hex_bytes(file.source->file_sha);
    if (!file_sha.has_value()) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> body;
    append_varint_field(body, 1, transfer_succeeded ? 1U : 0U);
    append_varint_field(body, 2, app_id);
    append_bytes_field(body, 3, *file_sha);
    append_string_field(body, 4, file.source->filename);
    return body;
}

std::vector<std::uint8_t> build_complete_app_upload_batch_request(std::uint32_t app_id,
                                                                  std::uint64_t batch_id,
                                                                  std::uint32_t batch_eresult) {
    std::vector<std::uint8_t> body;
    append_varint_field(body, 1, app_id);
    append_varint_field(body, 2, batch_id);
    append_varint_field(body, 3, batch_eresult);
    return body;
}

std::optional<SteamCloudFileEntry> parse_user_file_message(const std::vector<std::uint8_t>& bytes) {
    SteamCloudFileEntry entry;
    std::vector<std::string> platforms;
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
            entry.app_id = static_cast<std::uint32_t>(value);
            continue;
        }
        if (field_number == 2 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            entry.ugc_id = value;
            continue;
        }
        if (field_number == 3 && wire_type == 2) {
            if (!read_length_delimited_string(bytes, offset, entry.filename)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 4 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            entry.timestamp = value;
            continue;
        }
        if (field_number == 5 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            entry.file_size = static_cast<std::uint32_t>(value);
            continue;
        }
        if (field_number == 6 && wire_type == 2) {
            if (!read_length_delimited_string(bytes, offset, entry.url)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 7 && wire_type == 1) {
            if (!read_fixed64(bytes, offset, entry.steam_id_creator)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 8 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            entry.flags = static_cast<std::uint32_t>(value);
            continue;
        }
        if (field_number == 9 && wire_type == 2) {
            std::string platform;
            if (!read_length_delimited_string(bytes, offset, platform)) {
                return std::nullopt;
            }
            platforms.push_back(std::move(platform));
            continue;
        }
        if (field_number == 10 && wire_type == 2) {
            if (!read_length_delimited_string(bytes, offset, entry.file_sha)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 11 && wire_type == 0) {
            std::uint64_t compressed_size = 0;
            if (!read_varint(bytes, offset, compressed_size)) {
                return std::nullopt;
            }
            if (entry.file_size == 0) {
                entry.file_size = static_cast<std::uint32_t>(compressed_size);
            }
            continue;
        }
        if (!skip_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }

    entry.platforms_to_sync = join_strings(platforms);
    return entry;
}

std::optional<SteamCloudFileListResult> parse_enumerate_user_files_response_body(
    std::uint32_t app_id,
    const std::vector<std::uint8_t>& body,
    std::uint32_t eresult) {
    SteamCloudFileListResult result;
    result.ok = eresult == 1;
    result.app_id = app_id;
    result.eresult = eresult;

    std::size_t offset = 0;
    while (offset < body.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(body, offset, tag)) {
            return std::nullopt;
        }
        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);

        if (field_number == 1 && wire_type == 2) {
            std::vector<std::uint8_t> file_bytes;
            if (!read_length_delimited(body, offset, file_bytes)) {
                return std::nullopt;
            }
            auto entry = parse_user_file_message(file_bytes);
            if (!entry.has_value()) {
                return std::nullopt;
            }
            if (entry->app_id == 0) {
                entry->app_id = app_id;
            }
            result.files.push_back(std::move(*entry));
            continue;
        }
        if (field_number == 2 && wire_type == 0) {
            std::uint64_t total_files = 0;
            if (!read_varint(body, offset, total_files)) {
                return std::nullopt;
            }
            result.total_files = static_cast<std::uint32_t>(total_files);
            continue;
        }
        if (!skip_field(body, offset, wire_type)) {
            return std::nullopt;
        }
    }

    result.message = result.ok ? "ok" : "Cloud.EnumerateUserFiles failed";
    return result;
}

struct ParsedRequestHeader {
    std::string name;
    std::string value;
};

std::optional<ParsedRequestHeader> parse_http_header_message(const std::vector<std::uint8_t>& bytes) {
    ParsedRequestHeader header;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(bytes, offset, tag)) {
            return std::nullopt;
        }
        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);
        if (field_number == 1 && wire_type == 2) {
            if (!read_length_delimited_string(bytes, offset, header.name)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 2 && wire_type == 2) {
            if (!read_length_delimited_string(bytes, offset, header.value)) {
                return std::nullopt;
            }
            continue;
        }
        if (!skip_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }
    return header;
}

struct ClientFileDownloadResponse {
    std::uint32_t app_id = 0;
    std::uint32_t file_size = 0;
    std::uint32_t raw_file_size = 0;
    std::string url_host;
    std::string url_path;
    bool use_https = false;
    bool encrypted = false;
    std::vector<cauth::core::platform::HttpHeader> request_headers;
};

struct BeginAppUploadBatchResponse {
    std::uint64_t batch_id = 0;
};

std::optional<BeginAppUploadBatchResponse> parse_begin_app_upload_batch_response_body(
    const std::vector<std::uint8_t>& body) {
    BeginAppUploadBatchResponse result;
    std::size_t offset = 0;
    while (offset < body.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(body, offset, tag)) {
            return std::nullopt;
        }
        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);
        if (field_number == 1 && wire_type == 0) {
            if (!read_varint(body, offset, result.batch_id)) {
                return std::nullopt;
            }
            continue;
        }
        if (!skip_field(body, offset, wire_type)) {
            return std::nullopt;
        }
    }
    return result;
}

struct ClientFileUploadBlockRequest {
    std::string url_host;
    std::string url_path;
    bool use_https = true;
    std::uint32_t http_method = 0;
    std::uint64_t block_offset = 0;
    std::uint64_t block_length = 0;
    std::vector<std::uint8_t> explicit_body_data;
    std::vector<cauth::core::platform::HttpHeader> request_headers;
};

std::optional<ClientFileUploadBlockRequest> parse_client_file_upload_block_request(
    const std::vector<std::uint8_t>& bytes) {
    ClientFileUploadBlockRequest result;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(bytes, offset, tag)) {
            return std::nullopt;
        }
        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);
        if (field_number == 1 && wire_type == 2) {
            if (!read_length_delimited_string(bytes, offset, result.url_host)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 2 && wire_type == 2) {
            if (!read_length_delimited_string(bytes, offset, result.url_path)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 3 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            result.use_https = value != 0;
            continue;
        }
        if (field_number == 4 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return std::nullopt;
            }
            result.http_method = static_cast<std::uint32_t>(value);
            continue;
        }
        if (field_number == 5 && wire_type == 2) {
            std::vector<std::uint8_t> header_bytes;
            if (!read_length_delimited(bytes, offset, header_bytes)) {
                return std::nullopt;
            }
            const auto header = parse_http_header_message(header_bytes);
            if (!header.has_value()) {
                return std::nullopt;
            }
            result.request_headers.push_back({header->name, header->value});
            continue;
        }
        if (field_number == 6 && wire_type == 0) {
            if (!read_varint(bytes, offset, result.block_offset)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 7 && wire_type == 0) {
            if (!read_varint(bytes, offset, result.block_length)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 8 && wire_type == 2) {
            if (!read_length_delimited(bytes, offset, result.explicit_body_data)) {
                return std::nullopt;
            }
            continue;
        }
        if (!skip_field(bytes, offset, wire_type)) {
            return std::nullopt;
        }
    }
    return result;
}

struct ClientBeginFileUploadResponse {
    std::vector<ClientFileUploadBlockRequest> block_requests;
    bool encrypt_file = false;
};

std::optional<ClientBeginFileUploadResponse> parse_client_begin_file_upload_response_body(
    const std::vector<std::uint8_t>& body) {
    ClientBeginFileUploadResponse result;
    std::size_t offset = 0;
    while (offset < body.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(body, offset, tag)) {
            return std::nullopt;
        }
        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);
        if (field_number == 1 && wire_type == 2) {
            std::vector<std::uint8_t> block_bytes;
            if (!read_length_delimited(body, offset, block_bytes)) {
                return std::nullopt;
            }
            const auto block = parse_client_file_upload_block_request(block_bytes);
            if (!block.has_value()) {
                return std::nullopt;
            }
            result.block_requests.push_back(std::move(*block));
            continue;
        }
        if (field_number == 2 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(body, offset, value)) {
                return std::nullopt;
            }
            result.encrypt_file = value != 0;
            continue;
        }
        if (!skip_field(body, offset, wire_type)) {
            return std::nullopt;
        }
    }
    return result;
}

struct ClientCommitFileUploadResponse {
    bool file_committed = false;
};

std::optional<ClientCommitFileUploadResponse> parse_client_commit_file_upload_response_body(
    const std::vector<std::uint8_t>& body) {
    ClientCommitFileUploadResponse result;
    std::size_t offset = 0;
    while (offset < body.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(body, offset, tag)) {
            return std::nullopt;
        }
        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);
        if (field_number == 1 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(body, offset, value)) {
                return std::nullopt;
            }
            result.file_committed = value != 0;
            continue;
        }
        if (!skip_field(body, offset, wire_type)) {
            return std::nullopt;
        }
    }
    return result;
}

std::optional<ClientFileDownloadResponse> parse_client_file_download_response_body(
    const std::vector<std::uint8_t>& body) {
    ClientFileDownloadResponse result;
    std::size_t offset = 0;
    while (offset < body.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(body, offset, tag)) {
            return std::nullopt;
        }
        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);

        if (field_number == 1 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(body, offset, value)) {
                return std::nullopt;
            }
            result.app_id = static_cast<std::uint32_t>(value);
            continue;
        }
        if (field_number == 2 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(body, offset, value)) {
                return std::nullopt;
            }
            result.file_size = static_cast<std::uint32_t>(value);
            continue;
        }
        if (field_number == 3 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(body, offset, value)) {
                return std::nullopt;
            }
            result.raw_file_size = static_cast<std::uint32_t>(value);
            continue;
        }
        if (field_number == 7 && wire_type == 2) {
            if (!read_length_delimited_string(body, offset, result.url_host)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 8 && wire_type == 2) {
            if (!read_length_delimited_string(body, offset, result.url_path)) {
                return std::nullopt;
            }
            continue;
        }
        if (field_number == 9 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(body, offset, value)) {
                return std::nullopt;
            }
            result.use_https = value != 0;
            continue;
        }
        if (field_number == 10 && wire_type == 2) {
            std::vector<std::uint8_t> header_bytes;
            if (!read_length_delimited(body, offset, header_bytes)) {
                return std::nullopt;
            }
            const auto header = parse_http_header_message(header_bytes);
            if (!header.has_value()) {
                return std::nullopt;
            }
            result.request_headers.push_back({header->name, header->value});
            continue;
        }
        if (field_number == 11 && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(body, offset, value)) {
                return std::nullopt;
            }
            result.encrypted = value != 0;
            continue;
        }
        if (!skip_field(body, offset, wire_type)) {
            return std::nullopt;
        }
    }

    return result;
}

SteamCloudFileListResult make_cm_auth_error(std::uint32_t app_id, std::string message) {
    SteamCloudFileListResult result;
    result.app_id = app_id;
    result.message = std::move(message);
    return result;
}

SteamCloudDownloadResult make_download_error(std::string message) {
    SteamCloudDownloadResult result;
    result.message = std::move(message);
    return result;
}

SteamCloudUploadResult make_upload_error(std::string message) {
    SteamCloudUploadResult result;
    result.message = std::move(message);
    return result;
}

bool route_selection_applies_to_any_role(
    const cauth::core::platform::RouteSelection& selection,
    std::initializer_list<std::string_view> roles) {
    if (selection.empty() || selection.role.empty()) {
        return true;
    }
    for (const auto role : roles) {
        if (selection.role == role) {
            return true;
        }
    }
    return false;
}

void apply_selected_http_route(cauth::core::platform::RouteSelection const& selection,
                               std::initializer_list<std::string_view> roles,
                               std::string& url_host,
                               bool& use_https) {
    if (selection.empty() || !route_selection_applies_to_any_role(selection, roles)) {
        return;
    }
    if (!selection.endpoint.empty()) {
        url_host = selection.endpoint;
    }
    if (!selection.protocol.empty()) {
        if (selection.protocol == "https") {
            use_https = true;
        } else if (selection.protocol == "http") {
            use_https = false;
        }
    }
}

struct UploadBlockResult {
    bool ok = false;
    std::uint64_t bytes_transferred = 0;
    std::string message;
};

std::vector<cauth::core::platform::HttpHeader> sanitize_upload_headers(
    const std::vector<cauth::core::platform::HttpHeader>& headers) {
    std::vector<cauth::core::platform::HttpHeader> sanitized;
    sanitized.reserve(headers.size());
    for (const auto& header : headers) {
        if (header.name.size() == 14 &&
            std::equal(header.name.begin(), header.name.end(),
                       "Content-Length",
                       [](char lhs, char rhs) {
                           return std::tolower(static_cast<unsigned char>(lhs)) ==
                                  std::tolower(static_cast<unsigned char>(rhs));
                       })) {
            continue;
        }
        sanitized.push_back(header);
    }
    return sanitized;
}

UploadBlockResult upload_cm_block(const PreparedUploadFile& file,
                                  const ClientFileUploadBlockRequest& block,
                                  const cauth::core::platform::RouteSelection* route_selection,
                                  std::string_view filename,
                                  std::uint64_t file_offset,
                                  const SteamCloudUploadCallbacks& callbacks) {
    if (!cauth::core::platform::is_platform_http_client_available()) {
        return {false, 0, "platform HTTP client is not available"};
    }

    struct UploadProgressContext {
        std::string_view filename;
        std::uint64_t file_offset = 0;
        std::uint64_t total_bytes = 0;
        const SteamCloudUploadCallbacks* callbacks = nullptr;
        std::uint64_t last_bytes_transferred = 0;
    } progress_context{filename, file_offset, file.upload_bytes.size(), &callbacks, 0};

    if (file_offset > file.upload_bytes.size()) {
        return {false, 0, "CM Cloud upload offset is invalid"};
    }

    cauth::core::platform::HttpRequest http_request;
    http_request.method = block.http_method == 2 ? cauth::core::platform::HttpMethod::Post
                                                 : cauth::core::platform::HttpMethod::Put;
    auto block_host = block.url_host;
    auto block_use_https = block.use_https;
    if (route_selection != nullptr) {
        apply_selected_http_route(*route_selection, {"upload", "content"}, block_host, block_use_https);
    }
    http_request.url =
        std::string{block_use_https ? "https://" : "http://"} + block_host + block.url_path;
    http_request.headers = sanitize_upload_headers(block.request_headers);
    http_request.content_type = "application/octet-stream";

    if (!block.explicit_body_data.empty()) {
        if (file_offset > block.block_offset) {
            return {false, 0, "CM Cloud explicit upload block cannot resume from the middle"};
        }
        http_request.body_view = &block.explicit_body_data;
    } else {
        if (block.block_offset > file.upload_bytes.size() ||
            block.block_length > file.upload_bytes.size() - block.block_offset) {
            return {false, 0, "CM Cloud upload block range is invalid"};
        }
        const auto block_end = block.block_offset + block.block_length;
        if (file_offset > block_end) {
            return {false, 0, "CM Cloud upload resume offset exceeds the block range"};
        }
        const auto request_offset = (std::max)(block.block_offset, file_offset);
        http_request.body_view = &file.upload_bytes;
        http_request.body_offset = request_offset;
        http_request.body_length = block_end - request_offset;
    }

    http_request.callbacks.progress_hook =
        [](const cauth::core::platform::HttpTransferProgress& progress, void* user_data) {
            auto* context = static_cast<UploadProgressContext*>(user_data);
            if (context == nullptr || context->callbacks == nullptr ||
                context->callbacks->progress_hook == nullptr ||
                progress.direction != cauth::core::platform::HttpTransferDirection::Upload) {
                return;
            }
            context->last_bytes_transferred = progress.bytes_transferred;
            context->callbacks->progress_hook(
                context->filename,
                context->file_offset + progress.bytes_transferred,
                context->total_bytes,
                context->callbacks->user_data);
        };
    http_request.callbacks.cancel_hook =
        [](void* user_data) -> bool {
            const auto* context = static_cast<const UploadProgressContext*>(user_data);
            return context != nullptr && context->callbacks != nullptr &&
                   ((context->callbacks->pause_hook != nullptr &&
                     context->callbacks->pause_hook(context->callbacks->user_data)) ||
                    (context->callbacks->cancel_hook != nullptr &&
                     context->callbacks->cancel_hook(context->callbacks->user_data)));
        };
    http_request.callbacks.user_data = &progress_context;

    const auto response = cauth::core::platform::perform_platform_http_request(http_request);
    if (!response.ok) {
        return {false,
                progress_context.last_bytes_transferred,
                response.error_message.empty() ? "CM Cloud upload block HTTP request failed"
                                               : response.error_message};
    }
    return {true, progress_context.last_bytes_transferred, "ok"};
}

} // namespace

SteamCloudFileListResult fetch_remote_file_list_via_cm(const SteamCloudRequest& request,
                                                       std::uint32_t count,
                                                       std::uint32_t start_index,
                                                       bool extended_details) {
    if (request.app_id == 0) {
        return make_cm_auth_error(0, "app_id is required");
    }
    if (!request.session_type.empty() &&
        request.session_type != cauth::steam::auth::kSteamSessionTypeSteamClient) {
        return make_cm_auth_error(
            request.app_id,
            "CM Steam Cloud requires a steam-client session; sign in with `steam auth login`");
    }
    const auto session = make_cm_auth_session(request);
    if (!session.has_value()) {
        return make_cm_auth_error(
            request.app_id,
            "refresh token is required for CM Steam Cloud; sign in with steam auth login");
    }

    SteamCloudFileListResult final_result;
    final_result.app_id = request.app_id;
    SteamCmConnector connector;
    const auto operation = connector.with_logged_on_session(
        *session,
        5,
        request.route_selection.empty() ? nullptr : &request.route_selection,
        [&](const auto&, auto& cm_session) {
            const auto call = cm_session.call_service_method(
                "Cloud.EnumerateUserFiles#1",
                build_enumerate_user_files_request(
                    request.app_id, count, start_index, extended_details),
                kEnumerateUserFilesJobId);
            if (!call.ok) {
                return SteamCmAttemptResult{
                    is_retryable_cm_error(call.error_message) ? SteamCmContinuation::Continue
                                                              : SteamCmContinuation::Stop,
                    false,
                    "Cloud.EnumerateUserFiles failed: " + call.error_message,
                };
            }

            const auto parsed = parse_enumerate_user_files_response_body(
                request.app_id, call.body, call.header.eresult == 0 ? 1U : call.header.eresult);
            if (!parsed.has_value()) {
                return SteamCmAttemptResult{
                    SteamCmContinuation::Stop,
                    false,
                    "Cloud.EnumerateUserFiles response parse failed",
                };
            }
            final_result = *parsed;
            return SteamCmAttemptResult{SteamCmContinuation::Stop, true, ""};
        });

    if (!operation.ok) {
        return make_cm_auth_error(request.app_id, operation.error_message);
    }

    return final_result;
}

SteamCloudDownloadResult download_remote_file_via_cm(
    const SteamCloudRequest& request,
    const SteamCloudFileEntry& file,
    const SteamCloudDownloadOptions& download_options,
    const cauth::core::platform::HttpRequestCallbacks& callbacks) {
    if (request.app_id == 0) {
        return make_download_error("app_id is required");
    }
    if (!request.session_type.empty() &&
        request.session_type != cauth::steam::auth::kSteamSessionTypeSteamClient) {
        return make_download_error(
            "CM Steam Cloud requires a steam-client session; sign in with `steam auth login`");
    }
    const auto session = make_cm_auth_session(request);
    if (!session.has_value()) {
        return make_download_error(
            "refresh token is required for CM Steam Cloud; sign in with steam auth login");
    }
    if (!cauth::core::platform::is_platform_http_client_available()) {
        return make_download_error("platform HTTP client is not available");
    }

    ClientFileDownloadResponse download_info;
    SteamCmConnector connector;
    const auto operation = connector.with_logged_on_session(
        *session,
        5,
        request.route_selection.empty() ? nullptr : &request.route_selection,
        [&](const auto&, auto& cm_session) {
            const auto call = cm_session.call_service_method(
                "Cloud.ClientFileDownload#1",
                build_client_file_download_request(request.app_id, file.filename),
                kClientFileDownloadJobId);
            if (!call.ok) {
                return SteamCmAttemptResult{
                    is_retryable_cm_error(call.error_message) ? SteamCmContinuation::Continue
                                                              : SteamCmContinuation::Stop,
                    false,
                    "Cloud.ClientFileDownload failed: " + call.error_message,
                };
            }

            const auto parsed = parse_client_file_download_response_body(call.body);
            if (!parsed.has_value()) {
                return SteamCmAttemptResult{
                    SteamCmContinuation::Stop,
                    false,
                    "Cloud.ClientFileDownload response parse failed",
                };
            }
            download_info = *parsed;
            return SteamCmAttemptResult{SteamCmContinuation::Stop, true, ""};
        });

    if (!operation.ok) {
        return make_download_error(operation.error_message);
    }
    if (download_info.url_host.empty()) {
        return make_download_error("Cloud.ClientFileDownload did not return a download host");
    }

    auto download_host = download_info.url_host;
    auto download_use_https = download_info.use_https;
    apply_selected_http_route(request.route_selection, {"download", "content"}, download_host, download_use_https);

    cauth::core::platform::HttpRequest http_request;
    http_request.method = cauth::core::platform::HttpMethod::Get;
    http_request.url =
        std::string{download_use_https ? "https://" : "http://"} + download_host +
        download_info.url_path;
    http_request.headers = download_info.request_headers;
    http_request.callbacks = callbacks;
    http_request.use_range = download_options.use_range;
    http_request.range_start = download_options.range_start;
    http_request.response_write_hook = download_options.response_write_hook;
    http_request.response_write_user_data = download_options.response_write_user_data;
    const auto response = cauth::core::platform::perform_platform_http_request(http_request);
    if (!response.ok) {
        return make_download_error(response.error_message.empty()
                                       ? "CM Cloud file download HTTP request failed"
                                       : response.error_message);
    }

    SteamCloudDownloadResult result;
    result.ok = true;
    result.bytes = response.body;
    result.file_size = download_info.file_size;
    result.raw_file_size = download_info.raw_file_size;
    result.encrypted = download_info.encrypted;
    result.message = "ok";
    return result;
}

SteamCloudUploadResult upload_cloud_files_via_cm(const SteamCloudRequest& request,
                                                 std::string_view machine_name,
                                                 const std::vector<SteamCloudUploadFile>& files,
                                                 const std::vector<std::string>& files_to_delete,
                                                 const SteamCloudUploadCallbacks& callbacks) {
    if (request.app_id == 0) {
        return make_upload_error("app_id is required");
    }
    if (!request.session_type.empty() &&
        request.session_type != cauth::steam::auth::kSteamSessionTypeSteamClient) {
        return make_upload_error(
            "CM Steam Cloud requires a steam-client session; sign in with `steam auth login`");
    }
    const auto session = make_cm_auth_session(request);
    if (!session.has_value()) {
        return make_upload_error(
            "refresh token is required for CM Steam Cloud; sign in with steam auth login");
    }

    for (const auto& file : files) {
        if (file.filename.empty()) {
            return make_upload_error("CM Cloud upload file is missing a filename");
        }
        if (file.file_sha.empty()) {
            return make_upload_error("CM Cloud upload file is missing a SHA-1");
        }
        if (!decode_hex_bytes(file.file_sha).has_value()) {
            return make_upload_error("CM Cloud upload file SHA-1 is not valid hex");
        }
        if (file.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return make_upload_error("CM Cloud upload file is too large");
        }
    }
    std::vector<std::string> upload_filenames;
    upload_filenames.reserve(files.size());
    std::vector<PreparedUploadFile> prepared_files;
    prepared_files.reserve(files.size());
    for (const auto& file : files) {
        upload_filenames.push_back(file.filename);
        prepared_files.push_back(prepare_upload_file(file));
    }

    const auto resume_state_path = make_cm_upload_resume_state_path(request.local_root);
    const auto batch_token =
        make_cm_upload_batch_token(request, machine_name, prepared_files, files_to_delete);
    CmUploadResumeState resume_state;
    bool resume_state_loaded = false;
    {
        std::string resume_error;
        if (load_cm_upload_resume_state(resume_state_path, resume_state, resume_error)) {
            if (resume_state.token == batch_token) {
                resume_state_loaded = true;
            } else {
                std::string clear_error;
                (void)clear_cm_upload_resume_state(resume_state_path, clear_error);
            }
        }
    }

    SteamCloudUploadResult final_result;
    final_result.ok = false;
    final_result.resumable = !prepared_files.empty() || !files_to_delete.empty();
    final_result.resumed = resume_state_loaded;
    final_result.resume_from_bytes = resume_state_loaded ? resume_state.committed_bytes : 0;
    const auto make_current_upload_error = [&](std::string message) {
        auto result = make_upload_error(std::move(message));
        result.resumable = final_result.resumable;
        result.resumed = final_result.resumed;
        result.resume_from_bytes = final_result.resume_from_bytes;
        return result;
    };
    std::uint64_t batch_id = 0;
    SteamCmConnector connector;
    const auto operation = connector.with_logged_on_session(
        *session,
        5,
        request.route_selection.empty() ? nullptr : &request.route_selection,
        [&](const auto&, auto& cm_session) {
            std::string upload_error;
            bool pause_requested = false;
            bool cancel_requested = false;
            const PreparedUploadFile* current_file = nullptr;
            std::string current_file_token;

            if (callbacks.state_hook != nullptr) {
                callbacks.state_hook(
                    final_result.resumable,
                    final_result.resumed,
                    final_result.resume_from_bytes,
                    callbacks.user_data);
            }

            if (resume_state_loaded && resume_state.group_id != 0) {
                batch_id = resume_state.group_id;
            } else {
                const auto begin_call = cm_session.call_service_method(
                    "Cloud.BeginAppUploadBatch#1",
                    build_begin_app_upload_batch_request(
                        request.app_id, machine_name, upload_filenames, files_to_delete),
                    kBeginAppUploadBatchJobId);
                if (!begin_call.ok) {
                    return SteamCmAttemptResult{
                        is_retryable_cm_error(begin_call.error_message) ? SteamCmContinuation::Continue
                                                                        : SteamCmContinuation::Stop,
                        false,
                        "Cloud.BeginAppUploadBatch failed: " + begin_call.error_message,
                    };
                }

                const auto begin_response = parse_begin_app_upload_batch_response_body(begin_call.body);
                if (!begin_response.has_value()) {
                    return SteamCmAttemptResult{
                        SteamCmContinuation::Stop,
                        false,
                        "Cloud.BeginAppUploadBatch response parse failed",
                    };
                }
                batch_id = begin_response->batch_id;
                if (!prepared_files.empty() || !files_to_delete.empty()) {
                    std::string save_error;
                    const CmUploadResumeState initial_state{
                        batch_token,
                        0,
                        batch_id,
                        0,
                        prepared_files.empty() ? std::string{} :
                                                 make_cm_upload_file_token(request, prepared_files.front()),
                    };
                    if (!save_cm_upload_resume_state(resume_state_path, initial_state, save_error)) {
                        return SteamCmAttemptResult{
                            SteamCmContinuation::Stop,
                            false,
                            save_error,
                        };
                    }
                }
            }

            const auto start_file_index =
                resume_state_loaded ? (std::min)(resume_state.item_index, prepared_files.size()) : 0U;
            for (std::size_t file_index = start_file_index; file_index < prepared_files.size();
                 ++file_index) {
                const auto& file = prepared_files[file_index];
                current_file = &file;
                current_file_token = make_cm_upload_file_token(request, file);
                auto uploaded_file_bytes =
                    resume_state_loaded && file_index == resume_state.item_index &&
                            resume_state.item_token == current_file_token
                        ? resume_state.committed_bytes
                        : 0ULL;

                {
                    std::string save_error;
                    const CmUploadResumeState file_state{
                        batch_token,
                        uploaded_file_bytes,
                        batch_id,
                        file_index,
                        current_file_token,
                    };
                    if (!save_cm_upload_resume_state(resume_state_path, file_state, save_error)) {
                        return SteamCmAttemptResult{
                            SteamCmContinuation::Stop,
                            false,
                            save_error,
                        };
                    }
                }

                const auto request_body =
                    build_client_begin_file_upload_request(request.app_id, batch_id, file);
                if (!request_body.has_value()) {
                    upload_error =
                        "Cloud.ClientBeginFileUpload request build failed for " + file.source->filename;
                    break;
                }

                const auto begin_file_call = cm_session.call_service_method(
                    "Cloud.ClientBeginFileUpload#1",
                    *request_body,
                    kClientBeginFileUploadJobId,
                    12);
                if (!begin_file_call.ok) {
                    if (resume_state_loaded) {
                        std::string clear_error;
                        (void)clear_cm_upload_resume_state(resume_state_path, clear_error);
                    }
                    upload_error =
                        "Cloud.ClientBeginFileUpload failed for " + file.source->filename + ": " +
                        begin_file_call.error_message + " [raw_file_size=" +
                        std::to_string(file.raw_file_size) + ", upload_file_size=" +
                        std::to_string(file.upload_bytes.size()) + ", compressed=" +
                        (file.compressed ? "1" : "0") + ", timestamp=" +
                        std::to_string(file.timestamp) + "]";
                    break;
                }

                const auto begin_file_response =
                    parse_client_begin_file_upload_response_body(begin_file_call.body);
                if (!begin_file_response.has_value()) {
                    upload_error =
                        "Cloud.ClientBeginFileUpload response parse failed for " + file.source->filename;
                    break;
                }
                if (begin_file_response->encrypt_file) {
                    upload_error =
                        "CM Cloud upload requested encryption for " + file.source->filename +
                        ", which is not supported yet";
                    break;
                }

                bool transfer_succeeded = true;
                for (const auto& block : begin_file_response->block_requests) {
                    const auto block_end = block.block_offset + block.block_length;
                    if (uploaded_file_bytes >= block_end) {
                        continue;
                    }

                    const auto request_offset = uploaded_file_bytes > block.block_offset
                                                    ? uploaded_file_bytes
                                                    : block.block_offset;
                    const auto block_result = upload_cm_block(
                        file,
                        block,
                        &request.route_selection,
                        file.source->filename,
                        request_offset,
                        callbacks);
                    if (!block_result.ok) {
                        const auto interrupt_action = current_upload_interrupt_action(callbacks);
                        uploaded_file_bytes = request_offset + block_result.bytes_transferred;

                        if (interrupt_action == UploadInterruptAction::Pause) {
                            pause_requested = true;
                        } else if (interrupt_action == UploadInterruptAction::Cancel) {
                            cancel_requested = true;
                        }

                        if (!cancel_requested) {
                            std::string save_error;
                            const CmUploadResumeState file_state{
                                batch_token,
                                uploaded_file_bytes,
                                batch_id,
                                file_index,
                                current_file_token,
                            };
                            if (!save_cm_upload_resume_state(
                                    resume_state_path, file_state, save_error)) {
                                return SteamCmAttemptResult{
                                    SteamCmContinuation::Stop,
                                    false,
                                    save_error,
                                };
                            }
                        }

                        if (!pause_requested && !cancel_requested &&
                            request_offset > block.block_offset) {
                            const auto retry_result = upload_cm_block(
                                file,
                                block,
                                &request.route_selection,
                                file.source->filename,
                                block.block_offset,
                                callbacks);
                            if (retry_result.ok) {
                                uploaded_file_bytes = block_end;
                                std::string save_error;
                                const CmUploadResumeState file_state{
                                    batch_token,
                                    uploaded_file_bytes,
                                    batch_id,
                                    file_index,
                                    current_file_token,
                                };
                                if (!save_cm_upload_resume_state(
                                        resume_state_path, file_state, save_error)) {
                                    return SteamCmAttemptResult{
                                        SteamCmContinuation::Stop,
                                        false,
                                        save_error,
                                    };
                                }
                                continue;
                            }
                        }

                        transfer_succeeded = false;
                        upload_error =
                            "failed to upload block for " + file.source->filename + ": " +
                            block_result.message;
                        break;
                    }
                    uploaded_file_bytes = block_end;
                    std::string save_error;
                    const CmUploadResumeState file_state{
                        batch_token,
                        uploaded_file_bytes,
                        batch_id,
                        file_index,
                        current_file_token,
                    };
                    if (!save_cm_upload_resume_state(resume_state_path, file_state, save_error)) {
                        return SteamCmAttemptResult{
                            SteamCmContinuation::Stop,
                            false,
                            save_error,
                        };
                    }
                }

                if (pause_requested) {
                    final_result = make_current_upload_error("operation paused");
                    return SteamCmAttemptResult{SteamCmContinuation::Stop, false, final_result.message};
                }
                if (!transfer_succeeded && cancel_requested) {
                    break;
                }
                if (!transfer_succeeded) {
                    final_result = make_current_upload_error(upload_error);
                    return SteamCmAttemptResult{SteamCmContinuation::Stop, false, final_result.message};
                }

                const auto commit_request =
                    build_client_commit_file_upload_request(
                        request.app_id, file, transfer_succeeded);
                if (!commit_request.has_value()) {
                    upload_error =
                        "Cloud.ClientCommitFileUpload request build failed for " + file.source->filename;
                    break;
                }

                const auto commit_call = cm_session.call_service_method(
                    "Cloud.ClientCommitFileUpload#1",
                    *commit_request,
                    kClientCommitFileUploadJobId);
                if (!commit_call.ok) {
                    upload_error =
                        "Cloud.ClientCommitFileUpload failed for " + file.source->filename + ": " +
                        commit_call.error_message;
                    break;
                }

                const auto commit_response =
                    parse_client_commit_file_upload_response_body(commit_call.body);
                if (!commit_response.has_value()) {
                    upload_error =
                        "Cloud.ClientCommitFileUpload response parse failed for " + file.source->filename;
                    break;
                }
                if (!transfer_succeeded || !commit_response->file_committed) {
                    upload_error =
                        "Cloud.ClientCommitFileUpload did not commit " + file.source->filename;
                    break;
                }

                if (file_index + 1 < prepared_files.size()) {
                    std::string save_error;
                    const CmUploadResumeState next_state{
                        batch_token,
                        0,
                        batch_id,
                        file_index + 1,
                        make_cm_upload_file_token(request, prepared_files[file_index + 1]),
                    };
                    if (!save_cm_upload_resume_state(resume_state_path, next_state, save_error)) {
                        return SteamCmAttemptResult{
                            SteamCmContinuation::Stop,
                            false,
                            save_error,
                        };
                    }
                }
            }

            if (cancel_requested) {
                if (current_file != nullptr) {
                    const auto commit_request =
                        build_client_commit_file_upload_request(request.app_id, *current_file, false);
                    if (commit_request.has_value()) {
                        (void)cm_session.call_service_method(
                            "Cloud.ClientCommitFileUpload#1",
                            *commit_request,
                            kClientCommitFileUploadJobId);
                    }
                }

                const auto complete_call = cm_session.call_service_method(
                    "Cloud.CompleteAppUploadBatchBlocking#1",
                    build_complete_app_upload_batch_request(request.app_id, batch_id, 2U),
                    kCompleteAppUploadBatchJobId,
                    12);
                std::string clear_error;
                (void)clear_cm_upload_resume_state(resume_state_path, clear_error);
                final_result = make_current_upload_error(
                    complete_call.ok ? "operation canceled"
                                     : "operation canceled; complete batch failed: " +
                                           complete_call.error_message);
                return SteamCmAttemptResult{SteamCmContinuation::Stop, false, final_result.message};
            }

            if (!upload_error.empty()) {
                final_result = make_current_upload_error(upload_error);
                return SteamCmAttemptResult{SteamCmContinuation::Stop, false, final_result.message};
            }

            const auto batch_eresult = 1U;
            const auto complete_call = cm_session.call_service_method(
                "Cloud.CompleteAppUploadBatchBlocking#1",
                build_complete_app_upload_batch_request(request.app_id, batch_id, batch_eresult),
                kCompleteAppUploadBatchJobId,
                12);
            if (!complete_call.ok) {
                if (upload_error.empty()) {
                    return SteamCmAttemptResult{
                        is_retryable_cm_error(complete_call.error_message)
                            ? SteamCmContinuation::Continue
                            : SteamCmContinuation::Stop,
                        false,
                        "Cloud.CompleteAppUploadBatchBlocking failed: " + complete_call.error_message,
                    };
                }
                final_result = make_current_upload_error(
                    upload_error + "; complete batch failed: " + complete_call.error_message);
                return SteamCmAttemptResult{SteamCmContinuation::Stop, false, final_result.message};
            }

            std::string clear_error;
            if (!clear_cm_upload_resume_state(resume_state_path, clear_error) &&
                !clear_error.empty()) {
                return SteamCmAttemptResult{
                    SteamCmContinuation::Stop,
                    false,
                    clear_error,
                };
            }

            final_result.ok = true;
            final_result.message = "ok";
            return SteamCmAttemptResult{SteamCmContinuation::Stop, true, ""};
        });

    if (!operation.ok) {
        if (!final_result.message.empty()) {
            return final_result;
        }
        return make_current_upload_error(operation.error_message);
    }
    return final_result;
}

} // namespace cauth::steam::cloud
