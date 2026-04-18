#include "steam/depot/depot_file.hpp"

#include "steam/depot/depot_chunk.hpp"
#include "steam/depot/depot_hash.hpp"

#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <ostream>
#include <sstream>

namespace cauth::core::depot {
namespace {

std::string normalize_depot_path(std::string_view path) {
    std::string normalized;
    normalized.reserve(path.size());
    for (const auto ch : path) {
        if (ch == '/') {
            normalized.push_back('\\');
            continue;
        }
        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return normalized;
}

} // namespace

DepotFileResult validate_depot_file_layout(const DepotManifestFile& file) {
    std::uint64_t covered_size = 0;
    for (const auto& chunk : file.chunks) {
        if (chunk.uncompressed_size == 0) {
            return {false, "depot file contains an empty chunk"};
        }
        if (chunk.offset > std::numeric_limits<std::uint64_t>::max() - chunk.uncompressed_size) {
            return {false, "depot file chunk offset overflows"};
        }
        const auto chunk_end = chunk.offset + chunk.uncompressed_size;
        if (chunk_end > file.size) {
            return {false, "depot file chunk extends past file size"};
        }
        if (chunk_end > covered_size) {
            covered_size = chunk_end;
        }
    }
    if (covered_size != file.size) {
        return {false, "depot file chunks do not cover the declared file size"};
    }
    return {true, {}};
}

std::optional<std::size_t> find_depot_file_index(const DepotManifest& manifest,
                                                 std::string_view filename) {
    const auto normalized_filename = normalize_depot_path(filename);
    for (std::size_t index = 0; index < manifest.files.size(); ++index) {
        if (normalize_depot_path(manifest.files[index].filename) == normalized_filename) {
            return index;
        }
    }
    return std::nullopt;
}

DepotFileResult write_depot_file_chunk(std::ostream& output,
                                       const DepotManifestFile& file,
                                       std::size_t chunk_index,
                                       const std::vector<std::uint8_t>& chunk_bytes) {
    if (chunk_index >= file.chunks.size()) {
        return {false, "depot file chunk index is out of range"};
    }
    const auto& chunk = file.chunks[chunk_index];
    if (chunk_bytes.size() != chunk.uncompressed_size) {
        return {false, "processed chunk size does not match depot file metadata"};
    }
    if (chunk.offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return {false, "depot file chunk offset is too large for this platform"};
    }

    output.seekp(static_cast<std::streamoff>(chunk.offset), std::ios::beg);
    if (!output) {
        return {false, "failed to seek depot output file"};
    }
    output.write(reinterpret_cast<const char*>(chunk_bytes.data()),
                 static_cast<std::streamsize>(chunk_bytes.size()));
    if (!output) {
        return {false, "failed to write depot output file"};
    }
    return {true, {}};
}

DepotFileResult verify_depot_file_on_disk(const std::filesystem::path& path,
                                          const DepotManifestFile& file) {
    const auto layout = validate_depot_file_layout(file);
    if (!layout.ok) {
        return layout;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {false, "depot file is missing"};
    }
    if (ec) {
        return {false, "failed to query depot file path"};
    }
    if (!std::filesystem::is_regular_file(path, ec)) {
        return {false, "depot path is not a regular file"};
    }
    if (ec) {
        return {false, "failed to inspect depot file path"};
    }

    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        return {false, "failed to read depot file size"};
    }
    if (file_size != file.size) {
        return {false, "depot file size is incorrect"};
    }

    if (!file.content_sha.empty()) {
        return verify_depot_file_content_sha(path, file);
    }
    return verify_depot_file_chunk_checksums(path, file);
}

DepotFileResult verify_depot_file_chunk_checksums(const std::filesystem::path& path,
                                                  const DepotManifestFile& file) {
    if (file.size == 0) {
        return {true, {}};
    }
    if (file.chunks.empty()) {
        return {true, {}};
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return {false, "failed to open depot output file for chunk verification"};
    }

    for (std::size_t chunk_index = 0; chunk_index < file.chunks.size(); ++chunk_index) {
        const auto& chunk = file.chunks[chunk_index];
        if (chunk.offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
            return {false, "depot file chunk offset is too large for this platform"};
        }
        std::vector<std::uint8_t> chunk_bytes(chunk.uncompressed_size);
        input.seekg(static_cast<std::streamoff>(chunk.offset), std::ios::beg);
        if (!input) {
            return {false, "failed to seek depot output file for chunk verification"};
        }
        if (!chunk_bytes.empty()) {
            input.read(reinterpret_cast<char*>(chunk_bytes.data()),
                       static_cast<std::streamsize>(chunk_bytes.size()));
        }
        if (input.gcount() != static_cast<std::streamsize>(chunk_bytes.size())) {
            return {false, "failed to read depot output file for chunk verification"};
        }
        if (adler32_zero_seed(chunk_bytes) != chunk.checksum) {
            std::ostringstream error;
            error << "depot file chunk checksum is incorrect at chunk " << chunk_index;
            return {false, error.str()};
        }
    }
    return {true, {}};
}

DepotFileResult verify_depot_file_content_sha(const std::filesystem::path& path,
                                              const DepotManifestFile& file) {
    if (file.content_sha.empty()) {
        return {true, {}};
    }
    if (file.content_sha.size() != 20) {
        return {false, "manifest file content SHA-1 has an unexpected size"};
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return {false, "failed to open depot output file for SHA-1 verification"};
    }

    Sha1Hasher hasher;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read_size = input.gcount();
        if (read_size > 0) {
            hasher.update(reinterpret_cast<const std::uint8_t*>(buffer.data()),
                          static_cast<std::size_t>(read_size));
        }
    }
    if (!input.eof()) {
        return {false, "failed to read depot output file for SHA-1 verification"};
    }

    const auto digest = hasher.finish();
    if (!std::equal(digest.begin(), digest.end(), file.content_sha.begin())) {
        return {false, "depot file content SHA-1 is incorrect"};
    }
    return {true, {}};
}

bool depot_file_has_binary_verification(const DepotManifestFile& file) {
    return !file.content_sha.empty() || file.size == 0 || !file.chunks.empty();
}

} // namespace cauth::core::depot
