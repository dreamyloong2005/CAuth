#include "steam/depot/depot_manifest.hpp"
#include "core/crypto/aes.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#ifdef CAUTH_HAS_ZLIB
#include <zlib.h>
#endif

namespace cauth::core::depot {
namespace {

constexpr std::uint32_t kProtobufPayloadMagic = 0x71F617D0;
constexpr std::uint32_t kProtobufMetadataMagic = 0x1F4812BE;
constexpr std::uint32_t kProtobufSignatureMagic = 0x1B81B817;
constexpr std::uint32_t kProtobufEndOfManifestMagic = 0x32C415AB;
constexpr std::uint32_t kZipLocalFileHeaderMagic = 0x04034B50;

struct ManifestSections {
    std::vector<std::uint8_t> payload;
    std::vector<std::uint8_t> metadata;
    std::vector<std::uint8_t> signature;
};

bool read_u32_le(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                 std::uint32_t& value) {
    if (bytes.size() - offset < 4) {
        return false;
    }

    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
    offset += 4;
    return true;
}

bool read_u16_le(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                 std::uint16_t& value) {
    if (bytes.size() - offset < 2) {
        return false;
    }

    value = static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(bytes[offset + 1] << 8U);
    offset += 2;
    return true;
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

bool read_fixed32(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                  std::uint32_t& value) {
    return read_u32_le(bytes, offset, value);
}

bool read_length_delimited(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
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

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view encoded) {
    auto decode_char = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') {
            return ch - 'A';
        }
        if (ch >= 'a' && ch <= 'z') {
            return ch - 'a' + 26;
        }
        if (ch >= '0' && ch <= '9') {
            return ch - '0' + 52;
        }
        if (ch == '+') {
            return 62;
        }
        if (ch == '/') {
            return 63;
        }
        return -1;
    };

    std::vector<std::uint8_t> out;
    out.reserve((encoded.size() / 4) * 3);
    int buffer = 0;
    int bits = -8;
    for (const auto ch : encoded) {
        if (ch == '=') {
            break;
        }
        if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ') {
            continue;
        }

        const auto value = decode_char(ch);
        if (value < 0) {
            return std::nullopt;
        }
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}

bool skip_field(const std::vector<std::uint8_t>& bytes, std::size_t& offset, int wire_type) {
    std::uint64_t ignored = 0;
    std::vector<std::uint8_t> ignored_bytes;
    switch (wire_type) {
    case 0:
        return read_varint(bytes, offset, ignored);
    case 1:
        if (bytes.size() - offset < 8) {
            return false;
        }
        offset += 8;
        return true;
    case 2:
        return read_length_delimited(bytes, offset, ignored_bytes);
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

DepotManifestParseResult unwrap_zip_manifest(const std::vector<std::uint8_t>& bytes,
                                             std::vector<std::uint8_t>& out) {
    std::size_t offset = 4;
    std::uint16_t ignored_u16 = 0;
    std::uint16_t flags = 0;
    std::uint16_t compression_method = 0;
    std::uint32_t ignored_u32 = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint16_t filename_length = 0;
    std::uint16_t extra_length = 0;

    if (!read_u16_le(bytes, offset, ignored_u16) || !read_u16_le(bytes, offset, flags) ||
        !read_u16_le(bytes, offset, compression_method) ||
        !read_u16_le(bytes, offset, ignored_u16) || !read_u16_le(bytes, offset, ignored_u16) ||
        !read_u32_le(bytes, offset, ignored_u32) ||
        !read_u32_le(bytes, offset, compressed_size) ||
        !read_u32_le(bytes, offset, uncompressed_size) ||
        !read_u16_le(bytes, offset, filename_length) ||
        !read_u16_le(bytes, offset, extra_length)) {
        return {false, "zip manifest header is truncated", {}};
    }
    if ((flags & 0x08U) != 0) {
        return {false, "zip manifest uses a data descriptor, which is not supported yet", {}};
    }
    if (bytes.size() - offset < filename_length + extra_length ||
        bytes.size() - offset - filename_length - extra_length < compressed_size) {
        return {false, "zip manifest payload is truncated", {}};
    }

    offset += filename_length + extra_length;
    if (compression_method == 0) {
        out.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                   bytes.begin() + static_cast<std::ptrdiff_t>(offset + compressed_size));
        return {true, {}, {}};
    }
    if (compression_method != 8) {
        return {false, "zip manifest uses an unsupported compression method", {}};
    }

#ifdef CAUTH_HAS_ZLIB
    out.assign(uncompressed_size, 0);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(bytes.data() + offset));
    stream.avail_in = compressed_size;
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = uncompressed_size;

    auto result = inflateInit2(&stream, -MAX_WBITS);
    if (result != Z_OK) {
        return {false, "zlib inflateInit2 failed for zip manifest", {}};
    }
    result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END || stream.total_out != uncompressed_size) {
        return {false, "zlib inflate failed for zip manifest", {}};
    }
    return {true, {}, {}};
#else
    return {false, "zip manifest requires zlib support", {}};
#endif
}

DepotManifestParseResult parse_sections(const std::vector<std::uint8_t>& bytes,
                                         ManifestSections& sections) {
    std::size_t offset = 0;
    bool saw_end = false;
    while (offset < bytes.size()) {
        std::uint32_t magic = 0;
        if (!read_u32_le(bytes, offset, magic)) {
            return {false, "manifest section magic is truncated", {}};
        }
        if (magic == kProtobufEndOfManifestMagic) {
            saw_end = true;
            break;
        }

        std::uint32_t length = 0;
        if (!read_u32_le(bytes, offset, length) || bytes.size() - offset < length) {
            return {false, "manifest section payload is truncated", {}};
        }

        std::vector<std::uint8_t> section{
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + length),
        };
        offset += length;

        switch (magic) {
        case kProtobufPayloadMagic:
            sections.payload = std::move(section);
            break;
        case kProtobufMetadataMagic:
            sections.metadata = std::move(section);
            break;
        case kProtobufSignatureMagic:
            sections.signature = std::move(section);
            break;
        default:
            return {false, "unrecognized manifest section magic", {}};
        }
    }

    if (!saw_end) {
        return {false, "manifest end marker is missing", {}};
    }
    if (sections.payload.empty() || sections.metadata.empty()) {
        return {false, "manifest is missing payload or metadata section", {}};
    }
    return {true, {}, {}};
}

bool parse_metadata(const std::vector<std::uint8_t>& bytes, DepotManifest& manifest) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(bytes, offset, tag)) {
            return false;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);
        std::uint64_t value = 0;
        if (wire_type != 0) {
            if (!skip_field(bytes, offset, wire_type)) {
                return false;
            }
            continue;
        }
        if (!read_varint(bytes, offset, value)) {
            return false;
        }

        switch (field_number) {
        case 1:
            manifest.depot_id = static_cast<std::uint32_t>(value);
            break;
        case 2:
            manifest.manifest_gid = value;
            break;
        case 3:
            manifest.creation_time = static_cast<std::uint32_t>(value);
            break;
        case 4:
            manifest.filenames_encrypted = value != 0;
            break;
        case 5:
            manifest.total_uncompressed_size = value;
            break;
        case 6:
            manifest.total_compressed_size = value;
            break;
        case 7:
            manifest.unique_chunks = static_cast<std::uint32_t>(value);
            break;
        case 8:
            manifest.encrypted_crc = static_cast<std::uint32_t>(value);
            break;
        case 9:
            manifest.clear_crc = static_cast<std::uint32_t>(value);
            break;
        default:
            break;
        }
    }
    return true;
}

bool parse_chunk(const std::vector<std::uint8_t>& bytes, DepotManifestChunk& chunk) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(bytes, offset, tag)) {
            return false;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);
        if (field_number == 1 && wire_type == 2) {
            if (!read_length_delimited(bytes, offset, chunk.sha)) {
                return false;
            }
            continue;
        }
        if (field_number == 2 && wire_type == 5) {
            if (!read_fixed32(bytes, offset, chunk.checksum)) {
                return false;
            }
            continue;
        }
        if ((field_number == 3 || field_number == 4 || field_number == 5) && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return false;
            }
            if (field_number == 3) {
                chunk.offset = value;
            } else if (field_number == 4) {
                chunk.uncompressed_size = static_cast<std::uint32_t>(value);
            } else {
                chunk.compressed_size = static_cast<std::uint32_t>(value);
            }
            continue;
        }
        if (!skip_field(bytes, offset, wire_type)) {
            return false;
        }
    }
    return true;
}

bool parse_file_mapping(const std::vector<std::uint8_t>& bytes, DepotManifestFile& file) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(bytes, offset, tag)) {
            return false;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);
        if (field_number == 1 && wire_type == 2) {
            if (!read_length_delimited_string(bytes, offset, file.filename)) {
                return false;
            }
            continue;
        }
        if ((field_number == 2 || field_number == 3) && wire_type == 0) {
            std::uint64_t value = 0;
            if (!read_varint(bytes, offset, value)) {
                return false;
            }
            if (field_number == 2) {
                file.size = value;
            } else {
                file.flags = static_cast<std::uint32_t>(value);
            }
            continue;
        }
        if (field_number == 4 && wire_type == 2) {
            if (!read_length_delimited(bytes, offset, file.filename_sha)) {
                return false;
            }
            continue;
        }
        if (field_number == 5 && wire_type == 2) {
            if (!read_length_delimited(bytes, offset, file.content_sha)) {
                return false;
            }
            continue;
        }
        if (field_number == 6 && wire_type == 2) {
            std::vector<std::uint8_t> chunk_bytes;
            if (!read_length_delimited(bytes, offset, chunk_bytes)) {
                return false;
            }
            DepotManifestChunk chunk;
            if (!parse_chunk(chunk_bytes, chunk)) {
                return false;
            }
            file.chunks.push_back(std::move(chunk));
            continue;
        }
        if (field_number == 7 && wire_type == 2) {
            if (!read_length_delimited_string(bytes, offset, file.link_target)) {
                return false;
            }
            continue;
        }
        if (!skip_field(bytes, offset, wire_type)) {
            return false;
        }
    }
    return true;
}

bool parse_payload(const std::vector<std::uint8_t>& bytes, DepotManifest& manifest) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(bytes, offset, tag)) {
            return false;
        }

        const auto field_number = static_cast<int>(tag >> 3);
        const auto wire_type = static_cast<int>(tag & 0x07U);
        if (field_number == 1 && wire_type == 2) {
            std::vector<std::uint8_t> mapping_bytes;
            if (!read_length_delimited(bytes, offset, mapping_bytes)) {
                return false;
            }
            DepotManifestFile file;
            if (!parse_file_mapping(mapping_bytes, file)) {
                return false;
            }
            manifest.files.push_back(std::move(file));
            continue;
        }
        if (!skip_field(bytes, offset, wire_type)) {
            return false;
        }
    }
    return true;
}

DepotManifestDecryptResult decrypt_filename_in_place(std::string& filename,
                                                     const std::vector<std::uint8_t>& depot_key) {
    const auto decoded = base64_decode(filename);
    if (!decoded.has_value() || decoded->size() <= 16 || decoded->size() % 16 != 0) {
        return {false, "encrypted filename is not valid base64 AES data"};
    }
    const auto plain_result = cauth::core::crypto::aes256_ecb_then_cbc_decrypt_pkcs7(
        *decoded,
        depot_key);
    if (!plain_result.ok) {
        return {false, plain_result.error_message};
    }
    auto plain = plain_result.bytes;
    while (!plain.empty() && plain.back() == 0) {
        plain.pop_back();
    }
    std::replace(plain.begin(), plain.end(), static_cast<std::uint8_t>('/'),
                 static_cast<std::uint8_t>('\\'));
    filename.assign(plain.begin(), plain.end());
    return {true, {}};
}

} // namespace

DepotManifestParseResult parse_depot_manifest(const std::vector<std::uint8_t>& bytes) {
    const auto* manifest_bytes = &bytes;
    std::vector<std::uint8_t> unwrapped;
    if (bytes.size() >= 4) {
        std::size_t offset = 0;
        std::uint32_t magic = 0;
        if (read_u32_le(bytes, offset, magic) && magic == kZipLocalFileHeaderMagic) {
            if (const auto unwrap_result = unwrap_zip_manifest(bytes, unwrapped);
                !unwrap_result.ok) {
                return unwrap_result;
            }
            manifest_bytes = &unwrapped;
        }
    }

    ManifestSections sections;
    if (const auto sections_result = parse_sections(*manifest_bytes, sections);
        !sections_result.ok) {
        return sections_result;
    }

    DepotManifest manifest;
    if (!parse_metadata(sections.metadata, manifest)) {
        return {false, "manifest metadata protobuf is malformed", {}};
    }
    if (!parse_payload(sections.payload, manifest)) {
        return {false, "manifest payload protobuf is malformed", {}};
    }

    return {true, {}, std::move(manifest)};
}

DepotManifestDecryptResult decrypt_depot_manifest_filenames(
    DepotManifest& manifest,
    const std::vector<std::uint8_t>& depot_key) {
    if (!manifest.filenames_encrypted) {
        return {true, {}};
    }
    if (depot_key.size() != 32) {
        return {false, "depot key must be 32 bytes"};
    }

    for (auto& file : manifest.files) {
        if (auto result = decrypt_filename_in_place(file.filename, depot_key); !result.ok) {
            return result;
        }
        if (!file.link_target.empty()) {
            if (auto result = decrypt_filename_in_place(file.link_target, depot_key);
                !result.ok) {
                return result;
            }
        }
    }

    std::sort(manifest.files.begin(), manifest.files.end(), [](const auto& left, const auto& right) {
        return left.filename < right.filename;
    });
    manifest.filenames_encrypted = false;
    return {true, {}};
}

bool depot_file_is_directory(const DepotManifestFile& file) {
    return (file.flags & kDepotFileFlagDirectory) != 0;
}

} // namespace cauth::core::depot
