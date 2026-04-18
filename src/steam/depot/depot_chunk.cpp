#include "steam/depot/depot_chunk.hpp"
#include "core/crypto/aes.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#ifdef CAUTH_HAS_ZLIB
#include <zlib.h>
#endif

#ifdef CAUTH_HAS_ZSTD
#include <zstd.h>
#endif

extern "C" {
#include "LzmaDec.h"
}

namespace cauth::core::depot {
namespace {

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

void* lzma_alloc(ISzAllocPtr, std::size_t size) {
    return std::malloc(size);
}

void lzma_free(ISzAllocPtr, void* address) {
    std::free(address);
}

const ISzAlloc kLzmaAlloc{lzma_alloc, lzma_free};

std::uint32_t crc32_ieee(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

std::uint32_t crc32_ieee(const std::vector<std::uint8_t>& bytes) {
    return crc32_ieee(bytes.data(), bytes.size());
}

std::string lzma_error_message(SRes result) {
    switch (result) {
    case SZ_OK:
        return "ok";
    case SZ_ERROR_DATA:
        return "data error";
    case SZ_ERROR_MEM:
        return "memory allocation failed";
    case SZ_ERROR_UNSUPPORTED:
        return "unsupported properties";
    case SZ_ERROR_INPUT_EOF:
        return "unexpected end of input";
    default: {
        std::ostringstream out;
        out << "error " << result;
        return out.str();
    }
    }
}

DepotChunkProcessResult unzip_single_deflate_entry(const std::vector<std::uint8_t>& bytes,
                                                   std::uint32_t expected_size) {
    std::size_t offset = 0;
    std::uint32_t signature = 0;
    if (!read_u32_le(bytes, offset, signature) || signature != 0x04034B50) {
        return {false, "processed chunk is not a supported PKZip payload", {}};
    }

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
        return {false, "zip chunk header is truncated", {}};
    }
    if (compression_method != 0 && compression_method != 8) {
        return {false, "zip chunk uses an unsupported compression method", {}};
    }
    const bool uses_data_descriptor = (flags & 0x08U) != 0;
    if (!uses_data_descriptor && expected_size != 0 && uncompressed_size != expected_size) {
        return {false, "zip chunk uncompressed size does not match manifest metadata", {}};
    }
    if (bytes.size() - offset < filename_length + extra_length) {
        return {false, "zip chunk payload is truncated", {}};
    }

    offset += filename_length + extra_length;
    if (compression_method == 0) {
        const auto stored_size = uses_data_descriptor ? expected_size : uncompressed_size;
        if (stored_size == 0 && expected_size != 0) {
            return {false, "zip stored chunk size is missing", {}};
        }
        if (bytes.size() - offset < stored_size) {
            return {false, "zip stored chunk payload is truncated", {}};
        }
        std::vector<std::uint8_t> out(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                      bytes.begin() +
                                          static_cast<std::ptrdiff_t>(offset + stored_size));
        return {true, {}, std::move(out)};
    }

#ifdef CAUTH_HAS_ZLIB
    const auto payload_size = bytes.size() - offset;
    const auto out_size = uses_data_descriptor ? expected_size : uncompressed_size;
    if (out_size == 0 && expected_size != 0) {
        return {false, "zip deflate chunk size is missing", {}};
    }
    std::vector<std::uint8_t> out(out_size);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(bytes.data() + offset));
    stream.avail_in = static_cast<uInt>(payload_size);
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());

    auto result = inflateInit2(&stream, -MAX_WBITS);
    if (result != Z_OK) {
        return {false, "zlib inflateInit2 failed for chunk", {}};
    }
    result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END || stream.total_out != out.size()) {
        return {false, "zlib inflate failed for chunk", {}};
    }
    if (!uses_data_descriptor && stream.total_in != compressed_size) {
        return {false, "zip chunk compressed size does not match local header", {}};
    }
    if (uses_data_descriptor) {
        std::size_t descriptor_offset = offset + stream.total_in;
        std::uint32_t descriptor_signature_or_crc = 0;
        if (!read_u32_le(bytes, descriptor_offset, descriptor_signature_or_crc)) {
            return {false, "zip chunk data descriptor is truncated", {}};
        }
        std::uint32_t descriptor_crc = descriptor_signature_or_crc;
        if (descriptor_signature_or_crc == 0x08074B50U) {
            if (!read_u32_le(bytes, descriptor_offset, descriptor_crc)) {
                return {false, "zip chunk data descriptor CRC is truncated", {}};
            }
        }
        std::uint32_t descriptor_compressed_size = 0;
        std::uint32_t descriptor_uncompressed_size = 0;
        if (!read_u32_le(bytes, descriptor_offset, descriptor_compressed_size) ||
            !read_u32_le(bytes, descriptor_offset, descriptor_uncompressed_size)) {
            return {false, "zip chunk data descriptor sizes are truncated", {}};
        }
        if (descriptor_compressed_size != stream.total_in ||
            descriptor_uncompressed_size != out.size()) {
            return {false, "zip chunk data descriptor sizes do not match payload", {}};
        }
        if (crc32_ieee(out) != descriptor_crc) {
            return {false, "zip chunk data descriptor CRC is incorrect", {}};
        }
    }
    return {true, {}, std::move(out)};
#else
    return {false, "zip chunk processing requires zlib support", {}};
#endif
}

DepotChunkProcessResult decompress_vzip(const std::vector<std::uint8_t>& bytes,
                                        std::uint32_t expected_size) {
    constexpr std::size_t kHeaderSize = 7;
    constexpr std::size_t kLzmaPropsSize = 5;
    constexpr std::size_t kFooterSize = 10;
    if (bytes.size() < kHeaderSize + kLzmaPropsSize + kFooterSize) {
        return {false, "VZip chunk is truncated", {}};
    }
    if (bytes[0] != 'V' || bytes[1] != 'Z' || bytes[2] != 'a') {
        return {false, "VZip chunk header is invalid", {}};
    }
    if (bytes[bytes.size() - 2] != 'z' || bytes[bytes.size() - 1] != 'v') {
        return {false, "VZip chunk footer is invalid", {}};
    }

    std::size_t footer_offset = bytes.size() - kFooterSize;
    std::uint32_t footer_crc = 0;
    std::uint32_t footer_uncompressed_size = 0;
    if (!read_u32_le(bytes, footer_offset, footer_crc) ||
        !read_u32_le(bytes, footer_offset, footer_uncompressed_size)) {
        return {false, "VZip chunk footer is truncated", {}};
    }
    if (expected_size != 0 && footer_uncompressed_size != expected_size) {
        return {false, "VZip chunk uncompressed size does not match manifest metadata", {}};
    }

    const auto compressed_offset = kHeaderSize + kLzmaPropsSize;
    const auto compressed_size = bytes.size() - compressed_offset - kFooterSize;
    std::vector<std::uint8_t> out(footer_uncompressed_size);
    SizeT dest_size = out.size();
    SizeT source_size = compressed_size;
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    const auto result = LzmaDecode(out.data(), &dest_size, bytes.data() + compressed_offset,
                                   &source_size, bytes.data() + kHeaderSize, kLzmaPropsSize,
                                   LZMA_FINISH_ANY, &status, &kLzmaAlloc);
    if (result != SZ_OK) {
        return {false, "LZMA decode failed for VZip chunk: " + lzma_error_message(result), {}};
    }
    if (dest_size != out.size() || source_size != compressed_size) {
        return {false, "LZMA decoded sizes do not match VZip metadata", {}};
    }
    if (crc32_ieee(out) != footer_crc) {
        return {false, "VZip chunk CRC is incorrect", {}};
    }
    return {true, {}, std::move(out)};
}

DepotChunkProcessResult decompress_vzstd(const std::vector<std::uint8_t>& bytes,
                                         std::uint32_t expected_size) {
    if (bytes.size() < 23) {
        return {false, "VZstd chunk is truncated", {}};
    }
    if (bytes[0] != 'V' || bytes[1] != 'S' || bytes[2] != 'Z' || bytes[3] != 'a') {
        return {false, "VZstd chunk header is invalid", {}};
    }
    if (bytes[bytes.size() - 3] != 'z' || bytes[bytes.size() - 2] != 's' ||
        bytes[bytes.size() - 1] != 'v') {
        return {false, "VZstd chunk footer is invalid", {}};
    }

    std::size_t footer_offset = bytes.size() - 11;
    std::uint32_t footer_uncompressed_size = 0;
    if (!read_u32_le(bytes, footer_offset, footer_uncompressed_size)) {
        return {false, "VZstd chunk footer size is truncated", {}};
    }
    if (expected_size != 0 && footer_uncompressed_size != expected_size) {
        return {false, "VZstd chunk uncompressed size does not match manifest metadata", {}};
    }

#ifdef CAUTH_HAS_ZSTD
    constexpr std::size_t kHeaderSize = 8;
    constexpr std::size_t kFooterSize = 15;
    const auto compressed_size = bytes.size() - kHeaderSize - kFooterSize;
    std::vector<std::uint8_t> out(footer_uncompressed_size);
    const auto result = ZSTD_decompress(out.data(), out.size(), bytes.data() + kHeaderSize,
                                        compressed_size);
    if (ZSTD_isError(result) != 0) {
        return {false,
                std::string{"zstd decompress failed for VZstd chunk: "} +
                    ZSTD_getErrorName(result),
                {}};
    }
    if (result != out.size()) {
        return {false, "zstd decompressed size does not match VZstd footer", {}};
    }
    return {true, {}, std::move(out)};
#else
    return {false, "VZstd chunk processing requires zstd support", {}};
#endif
}

DepotChunkProcessResult aes_decrypt_chunk(const std::vector<std::uint8_t>& raw_bytes,
                                          const std::vector<std::uint8_t>& depot_key) {
    if (raw_bytes.size() <= 16 || raw_bytes.size() % 16 != 0) {
        return {false, "raw chunk is not valid AES data", {}};
    }
    const auto plain_result = cauth::core::crypto::aes256_ecb_then_cbc_decrypt_pkcs7(
        raw_bytes,
        depot_key);
    if (!plain_result.ok) {
        return {false, plain_result.error_message, {}};
    }
    return {true, {}, plain_result.bytes};
}

} // namespace

std::uint32_t adler32_zero_seed(const std::vector<std::uint8_t>& bytes) {
    constexpr std::uint32_t kMod = 65521;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    for (const auto byte : bytes) {
        a = (a + byte) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16U) | a;
}

DepotChunkProcessResult process_depot_chunk(const DepotManifestChunk& chunk,
                                            const std::vector<std::uint8_t>& raw_bytes,
                                            const std::vector<std::uint8_t>& depot_key) {
    if (depot_key.size() != 32) {
        return {false, "depot key must be 32 bytes", {}};
    }
    if (chunk.compressed_size != 0 && raw_bytes.size() != chunk.compressed_size) {
        return {false, "raw chunk size does not match manifest metadata", {}};
    }

    auto decrypted = aes_decrypt_chunk(raw_bytes, depot_key);
    if (!decrypted.ok) {
        return decrypted;
    }
    if (decrypted.bytes.size() < 4) {
        return {false, "decrypted chunk is too small", {}};
    }

    DepotChunkProcessResult processed;
    if (decrypted.bytes[0] == 'P' && decrypted.bytes[1] == 'K' &&
        decrypted.bytes[2] == 0x03 && decrypted.bytes[3] == 0x04) {
        processed = unzip_single_deflate_entry(decrypted.bytes, chunk.uncompressed_size);
    } else if (decrypted.bytes[0] == 'V' && decrypted.bytes[1] == 'Z' &&
               decrypted.bytes[2] == 'a') {
        processed = decompress_vzip(decrypted.bytes, chunk.uncompressed_size);
    } else if (decrypted.bytes[0] == 'V' && decrypted.bytes[1] == 'S' &&
               decrypted.bytes[2] == 'Z' && decrypted.bytes[3] == 'a') {
        processed = decompress_vzstd(decrypted.bytes, chunk.uncompressed_size);
    } else {
        return {false, "unexpected depot chunk compression", {}};
    }
    if (!processed.ok) {
        return processed;
    }

    if (processed.bytes.size() != chunk.uncompressed_size) {
        return {false, "processed chunk size does not match manifest metadata", {}};
    }
    if (adler32_zero_seed(processed.bytes) != chunk.checksum) {
        return {false, "processed chunk checksum is incorrect", {}};
    }
    return processed;
}

} // namespace cauth::core::depot
