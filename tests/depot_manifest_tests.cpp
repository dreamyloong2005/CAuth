#include "steam/depot/depot_manifest.hpp"
#include "steam/depot/depot_chunk.hpp"
#include "steam/depot/depot_file.hpp"
#include "steam/depot/depot_hash.hpp"
#include "steam/depot/steam_depot_application.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t kPayloadMagic = 0x71F617D0;
constexpr std::uint32_t kMetadataMagic = 0x1F4812BE;
constexpr std::uint32_t kSignatureMagic = 0x1B81B817;
constexpr std::uint32_t kEndMagic = 0x32C415AB;

void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void append_varint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
        value >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

void append_tag(std::vector<std::uint8_t>& out, int field, int wire_type) {
    append_varint(out, static_cast<std::uint64_t>((field << 3) | wire_type));
}

void append_varint_field(std::vector<std::uint8_t>& out, int field, std::uint64_t value) {
    append_tag(out, field, 0);
    append_varint(out, value);
}

void append_fixed32_field(std::vector<std::uint8_t>& out, int field, std::uint32_t value) {
    append_tag(out, field, 5);
    append_u32_le(out, value);
}

void append_bytes_field(std::vector<std::uint8_t>& out, int field,
                        const std::vector<std::uint8_t>& value) {
    append_tag(out, field, 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void append_string_field(std::vector<std::uint8_t>& out, int field, std::string_view value) {
    append_tag(out, field, 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void append_section(std::vector<std::uint8_t>& out, std::uint32_t magic,
                    const std::vector<std::uint8_t>& value) {
    append_u32_le(out, magic);
    append_u32_le(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> make_chunk() {
    std::vector<std::uint8_t> chunk;
    append_bytes_field(chunk, 1, std::vector<std::uint8_t>(20, 0x11));
    append_fixed32_field(chunk, 2, 0x12345678);
    append_varint_field(chunk, 3, 4096);
    append_varint_field(chunk, 4, 8192);
    append_varint_field(chunk, 5, 1024);
    return chunk;
}

std::vector<std::uint8_t> make_payload() {
    std::vector<std::uint8_t> file;
    append_string_field(file, 1, "hl2/tf/test.txt");
    append_varint_field(file, 2, 8192);
    append_varint_field(file, 3, 0);
    append_bytes_field(file, 4, std::vector<std::uint8_t>(20, 0x22));
    append_bytes_field(file, 5, std::vector<std::uint8_t>(20, 0x33));
    append_bytes_field(file, 6, make_chunk());

    std::vector<std::uint8_t> payload;
    append_bytes_field(payload, 1, file);
    return payload;
}

std::vector<std::uint8_t> make_metadata() {
    std::vector<std::uint8_t> metadata;
    append_varint_field(metadata, 1, 441);
    append_varint_field(metadata, 2, 257913086909807568ULL);
    append_varint_field(metadata, 3, 1710000000);
    append_varint_field(metadata, 4, 1);
    append_varint_field(metadata, 5, 8192);
    append_varint_field(metadata, 6, 1024);
    append_varint_field(metadata, 7, 1);
    append_varint_field(metadata, 8, 1234);
    append_varint_field(metadata, 9, 5678);
    return metadata;
}

std::vector<std::uint8_t> make_manifest() {
    std::vector<std::uint8_t> manifest;
    append_section(manifest, kPayloadMagic, make_payload());
    append_section(manifest, kMetadataMagic, make_metadata());
    append_section(manifest, kSignatureMagic, {});
    append_u32_le(manifest, kEndMagic);
    return manifest;
}

} // namespace

int main() {
    const auto parsed = cauth::core::depot::parse_depot_manifest(make_manifest());
    if (!parsed.ok || parsed.manifest.depot_id != 441 ||
        parsed.manifest.manifest_gid != 257913086909807568ULL ||
        !parsed.manifest.filenames_encrypted || parsed.manifest.files.size() != 1 ||
        parsed.manifest.files[0].filename != "hl2/tf/test.txt" ||
        parsed.manifest.files[0].chunks.size() != 1 ||
        parsed.manifest.files[0].chunks[0].compressed_size != 1024 ||
        parsed.manifest.files[0].chunks[0].uncompressed_size != 8192) {
        std::cerr << "protobuf depot manifest should parse metadata, files, and chunks\n";
        return 1;
    }

    auto truncated = make_manifest();
    truncated.pop_back();
    const auto truncated_result = cauth::core::depot::parse_depot_manifest(truncated);
    if (truncated_result.ok) {
        std::cerr << "truncated depot manifest should be rejected\n";
        return 1;
    }

    if (cauth::core::depot::adler32_zero_seed(
            std::vector<std::uint8_t>{'W', 'i', 'k', 'i', 'p', 'e', 'd', 'i', 'a'}) !=
        0x11dd0397U) {
        std::cerr << "depot chunk Adler32 should use Steam's zero seed\n";
        return 1;
    }
    if (!cauth::core::depot::sha1_matches(
            std::vector<std::uint8_t>{'a', 'b', 'c'},
            std::vector<std::uint8_t>{0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81,
                                      0x6a, 0xba, 0x3e, 0x25, 0x71, 0x78, 0x50,
                                      0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d})) {
        std::cerr << "depot SHA-1 helper should match known vectors\n";
        return 1;
    }

    cauth::core::depot::DepotManifestFile depot_file;
    depot_file.filename = "folder\\file.bin";
    depot_file.size = 5;
    depot_file.chunks.push_back(cauth::core::depot::DepotManifestChunk{
        {},
        cauth::core::depot::adler32_zero_seed(std::vector<std::uint8_t>{'A', 'B'}),
        0,
        0,
        2,
    });
    depot_file.chunks.push_back(cauth::core::depot::DepotManifestChunk{
        {},
        cauth::core::depot::adler32_zero_seed(std::vector<std::uint8_t>{'C', 'D', 'E'}),
        2,
        0,
        3,
    });
    const auto layout = cauth::core::depot::validate_depot_file_layout(depot_file);
    if (!layout.ok) {
        std::cerr << "valid depot file layout should be accepted\n";
        return 1;
    }
    cauth::core::depot::DepotManifest depot_file_manifest;
    depot_file_manifest.files.push_back(depot_file);
    const auto matched_index =
        cauth::core::depot::find_depot_file_index(depot_file_manifest, "FOLDER/file.bin");
    if (!matched_index.has_value() || *matched_index != 0) {
        std::cerr << "depot file path lookup should normalize separators and case\n";
        return 1;
    }
    std::stringstream output{std::ios::in | std::ios::out | std::ios::binary};
    output.write(std::string(depot_file.size, '\0').data(),
                 static_cast<std::streamsize>(depot_file.size));
    const auto second_chunk = cauth::core::depot::write_depot_file_chunk(
        output, depot_file, 1, std::vector<std::uint8_t>{'C', 'D', 'E'});
    const auto first_chunk = cauth::core::depot::write_depot_file_chunk(
        output, depot_file, 0, std::vector<std::uint8_t>{'A', 'B'});
    output.seekg(0, std::ios::beg);
    std::string assembled(5, '\0');
    output.read(assembled.data(), static_cast<std::streamsize>(assembled.size()));
    if (!first_chunk.ok || !second_chunk.ok || assembled != "ABCDE") {
        std::cerr << "depot file chunks should write to manifest offsets\n";
        return 1;
    }
    const auto content_digest =
        cauth::core::depot::sha1_digest(std::vector<std::uint8_t>{'A', 'B', 'C', 'D', 'E'});
    depot_file.content_sha.assign(content_digest.begin(), content_digest.end());
    auto chunk_verified_file = depot_file;
    chunk_verified_file.content_sha.clear();
    const auto temp_file_path =
        std::filesystem::temp_directory_path() / "cauth_depot_file_sha_test.bin";
    {
        std::ofstream temp_output{temp_file_path, std::ios::binary | std::ios::trunc};
        temp_output << "ABCDE";
    }
    const auto content_sha_result =
        cauth::core::depot::verify_depot_file_content_sha(temp_file_path, depot_file);
    const auto chunk_verify_result =
        cauth::core::depot::verify_depot_file_chunk_checksums(temp_file_path, chunk_verified_file);
    {
        std::ofstream temp_output{temp_file_path, std::ios::binary | std::ios::trunc};
        temp_output << "abcde";
    }
    const auto bad_content_sha_result =
        cauth::core::depot::verify_depot_file_content_sha(temp_file_path, depot_file);
    const auto full_verify_bad_result =
        cauth::core::depot::verify_depot_file_on_disk(temp_file_path, chunk_verified_file);
    std::filesystem::remove(temp_file_path);
    if (!content_sha_result.ok || !chunk_verify_result.ok ||
        bad_content_sha_result.ok || full_verify_bad_result.ok) {
        std::cerr << "depot file content SHA-1 verification should accept matching files "
                     "and reject corrupted files\n";
        return 1;
    }

    cauth::steam::depot::LoadedDepotManifest loaded_manifest;
    loaded_manifest.manifest.files.push_back(chunk_verified_file);
    const auto verify_root =
        std::filesystem::temp_directory_path() / "cauth_verify_local_test";
    std::filesystem::remove_all(verify_root);
    std::filesystem::create_directories(verify_root / "folder");
    {
        std::ofstream temp_output{verify_root / "folder" / "file.bin", std::ios::binary | std::ios::trunc};
        temp_output << "ABCDE";
    }
    std::ostringstream verify_out;
    std::ostringstream verify_err;
    const auto verify_ok = cauth::steam::depot::verify_local_files_against_manifest(
        loaded_manifest,
        verify_root.string(),
        {},
        verify_out,
        verify_err);
    {
        std::ofstream temp_output{verify_root / "folder" / "file.bin", std::ios::binary | std::ios::trunc};
        temp_output << "abcde";
    }
    std::ostringstream verify_bad_out;
    std::ostringstream verify_bad_err;
    const auto verify_bad = cauth::steam::depot::verify_local_files_against_manifest(
        loaded_manifest,
        verify_root.string(),
        {},
        verify_bad_out,
        verify_bad_err);
    std::filesystem::remove(verify_root / "folder" / "file.bin");
    std::ostringstream verify_missing_out;
    std::ostringstream verify_missing_err;
    const auto verify_missing = cauth::steam::depot::verify_local_files_against_manifest(
        loaded_manifest,
        verify_root.string(),
        {},
        verify_missing_out,
        verify_missing_err);
    std::filesystem::remove_all(verify_root);
    if (verify_ok != 0 || verify_bad == 0 || verify_missing == 0 ||
        verify_out.str().find("OK file=folder\\file.bin") == std::string::npos ||
        verify_bad_out.str().find("MISMATCH file=folder\\file.bin") == std::string::npos ||
        verify_missing_out.str().find("MISSING file=folder\\file.bin") == std::string::npos) {
        std::cerr << "manifest local verification should report ok, mismatch, and missing files\n";
        return 1;
    }

    cauth::core::depot::DepotManifestFile directory_entry;
    directory_entry.filename = "folder";
    directory_entry.flags = cauth::core::depot::kDepotFileFlagDirectory;
    cauth::steam::depot::LoadedDepotManifest directory_manifest;
    directory_manifest.manifest.files.push_back(directory_entry);
    directory_manifest.manifest.files.push_back(chunk_verified_file);
    std::ostringstream directory_list_out;
    const auto directory_list_exit = cauth::steam::depot::print_file_list(
        directory_manifest,
        {},
        10,
        directory_list_out);
    if (directory_list_exit != 0 ||
        directory_list_out.str().find("file[0]=folder") != std::string::npos ||
        directory_list_out.str().find("file[1]=folder\\file.bin") == std::string::npos ||
        directory_list_out.str().find("total=1") == std::string::npos) {
        std::cerr << "manifest file list should skip directory entries\n";
        return 1;
    }

    std::ostringstream directory_select_err;
    const auto directory_selection = cauth::steam::depot::resolve_file_selection(
        directory_manifest,
        0,
        true,
        {},
        directory_select_err);
    if (directory_selection.has_value() ||
        directory_select_err.str().find("directory") == std::string::npos) {
        std::cerr << "directory entries should be rejected as downloadable files\n";
        return 1;
    }

    const auto verify_root_with_directory =
        std::filesystem::temp_directory_path() / "cauth_verify_local_directory_test";
    std::filesystem::remove_all(verify_root_with_directory);
    std::filesystem::create_directories(verify_root_with_directory / "folder");
    {
        std::ofstream temp_output{
            verify_root_with_directory / "folder" / "file.bin",
            std::ios::binary | std::ios::trunc};
        temp_output << "ABCDE";
    }
    cauth::steam::depot::LocalVerifyReport directory_verify_report;
    std::ostringstream directory_verify_out;
    std::ostringstream directory_verify_err;
    const auto directory_verify_exit = cauth::steam::depot::verify_local_files_against_manifest(
        directory_manifest,
        verify_root_with_directory.string(),
        {},
        directory_verify_out,
        directory_verify_err,
        &directory_verify_report);
    std::filesystem::remove_all(verify_root_with_directory);
    if (directory_verify_exit != 0 ||
        directory_verify_report.total_count != 1 ||
        directory_verify_report.checked_count != 1 ||
        directory_verify_out.str().find("file=folder path=") != std::string::npos) {
        std::cerr << "local verify should skip directory entries\n";
        return 1;
    }

    cauth::core::depot::DepotManifest encrypted_manifest;
    encrypted_manifest.filenames_encrypted = true;
    encrypted_manifest.files.push_back(cauth::core::depot::DepotManifestFile{
        "YaaTbk6PEBwcwfmTtUKg1Hh6ayQzKHnc7dlrReeEz2JJab3MsEKXy9CJQr6hWuV2"});
    std::vector<std::uint8_t> depot_key;
    for (std::uint8_t value = 0; value < 32; ++value) {
        depot_key.push_back(value);
    }

    const auto decrypt_result =
        cauth::core::depot::decrypt_depot_manifest_filenames(encrypted_manifest, depot_key);
    if (!decrypt_result.ok || encrypted_manifest.filenames_encrypted ||
        encrypted_manifest.files[0].filename != "folder\\test.txt") {
        std::cerr << "encrypted manifest filenames should decrypt with depot key\n";
        return 1;
    }

    return 0;
}
