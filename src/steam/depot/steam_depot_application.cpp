#include "steam/depot/steam_depot_application.hpp"

#include "core/platform/session_repository_factory.hpp"
#include "steam/cm/cm_message.hpp"
#include "steam/auth/steam_auth_provider.hpp"
#include "steam/depot/cdn_directory.hpp"
#include "steam/depot/depot_chunk.hpp"
#include "steam/depot/depot_file.hpp"
#include "steam/depot/depot_cm_client.hpp"
#include "steam/depot/manifest_downloader.hpp"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_set>

namespace cauth::steam::depot {
namespace {

thread_local DepotDownloadProgressHook g_download_progress_hook = nullptr;
thread_local DepotDownloadCancelHook g_download_cancel_hook = nullptr;
thread_local void* g_download_hook_user_data = nullptr;

std::string lowercase_ascii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const auto ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered;
}

bool contains_ascii_case_insensitive(std::string_view value, std::string_view needle) {
    if (needle.empty()) return true;
    return lowercase_ascii(value).find(lowercase_ascii(needle)) != std::string::npos;
}

void append_depot_platform_summary(std::ostream& out, const cauth::core::depot::DepotInfo& depot) {
    out << " platform=" << cauth::core::depot::depot_platform_label(depot.os_list, depot.os_arch);
    if (!depot.depot_from_app.empty()) {
        out << " from_app=" << depot.depot_from_app;
    }
    if (depot.shared_install) {
        out << " shared_install=true";
    }
}

bool ensure_parent_directory_exists(const std::string& output_path, std::ostream& err) {
    std::error_code ec;
    const std::filesystem::path path{output_path};
    const auto parent = path.parent_path();
    if (parent.empty()) {
        return true;
    }
    if (std::filesystem::exists(parent, ec)) {
        return true;
    }
    ec.clear();
    if (std::filesystem::create_directories(parent, ec)) {
        return true;
    }
    if (std::filesystem::exists(parent, ec)) {
        return true;
    }
    err << "Failed to create output directory: " << parent.string();
    if (ec) {
        err << " (" << ec.message() << ')';
    }
    err << '\n';
    return false;
}

std::optional<std::filesystem::path> make_safe_manifest_output_path(
    const std::string& output_root,
    std::string_view manifest_filename,
    std::ostream& err) {
    if (output_root.empty()) {
        err << "Output root is required\n";
        return std::nullopt;
    }

    std::filesystem::path relative_path;
    std::string segment;
    const auto flush_segment = [&]() -> bool {
        if (segment.empty() || segment == ".") {
            segment.clear();
            return true;
        }
        if (segment == "..") {
            err << "Unsafe manifest file path: " << manifest_filename << '\n';
            return false;
        }
        relative_path /= segment;
        segment.clear();
        return true;
    };

    for (const char ch : manifest_filename) {
        if (ch == '/' || ch == '\\') {
            if (!flush_segment()) {
                return std::nullopt;
            }
            continue;
        }
        segment.push_back(ch);
    }
    if (!flush_segment()) {
        return std::nullopt;
    }
    if (relative_path.empty()) {
        err << "Manifest file path is empty: " << manifest_filename << '\n';
        return std::nullopt;
    }
    return std::filesystem::path{output_root} / relative_path;
}

bool is_download_canceled() {
    return g_download_cancel_hook != nullptr && g_download_cancel_hook(g_download_hook_user_data);
}

bool fail_if_download_canceled(std::ostream& err) {
    if (!is_download_canceled()) {
        return false;
    }
    err << "operation canceled\n";
    return true;
}

void report_download_progress(const DepotDownloadProgress& progress) {
    if (g_download_progress_hook == nullptr) {
        return;
    }
    g_download_progress_hook(progress, g_download_hook_user_data);
}

std::vector<cauth::core::depot::CdnServer> prepare_cdn_servers_for_download(
    std::vector<cauth::core::depot::CdnServer> servers) {
    std::vector<cauth::core::depot::CdnServer> unique_servers;
    unique_servers.reserve(servers.size());
    std::unordered_set<std::string> seen;
    for (auto& server : servers) {
        if (server.protocol != cauth::core::depot::CdnServerProtocol::Https) {
            continue;
        }
        const auto authority = (server.vhost.empty() ? server.host : server.vhost) + ":" +
                               std::to_string(server.port);
        const auto key = "https://" + authority;
        if (!seen.insert(key).second) {
            continue;
        }
        unique_servers.push_back(std::move(server));
    }
    return unique_servers;
}

std::optional<std::vector<cauth::core::depot::CdnServer>> fetch_cdn_servers_for_download(
    std::uint32_t max_count,
    std::ostream& err) {
    cauth::core::depot::CdnDirectoryClient cdn_directory;
    const auto requested_server_count = std::max<std::uint32_t>(max_count, 20);
    const auto cdn_servers = cdn_directory.get_servers_for_steampipe(
        cauth::core::depot::CdnServerQuery{0, requested_server_count});
    if (!cdn_servers.ok) {
        err << "CDN directory lookup failed: " << cdn_servers.error_message << '\n';
        return std::nullopt;
    }
    auto prepared = prepare_cdn_servers_for_download(cdn_servers.servers);
    if (prepared.empty()) {
        err << "CDN directory lookup returned no HTTPS-capable servers\n";
        return std::nullopt;
    }
    return prepared;
}

std::uint64_t count_non_directory_manifest_files(const cauth::core::depot::DepotManifest& manifest) {
    std::uint64_t count = 0;
    for (const auto& file : manifest.files) {
        if (!cauth::core::depot::depot_file_is_directory(file)) {
            ++count;
        }
    }
    return count;
}

bool ensure_manifest_directory_exists(const std::string& output_root,
                                      std::string_view manifest_filename,
                                      std::ostream& err) {
    const auto output_path = make_safe_manifest_output_path(output_root, manifest_filename, err);
    if (!output_path.has_value()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(*output_path, ec);
    if (ec && !std::filesystem::is_directory(*output_path)) {
        err << "Failed to create directory path: " << output_path->string();
        if (ec) {
            err << " (" << ec.message() << ")";
        }
        err << '\n';
        return false;
    }
    return true;
}

} // namespace

void set_current_thread_depot_download_hooks(DepotDownloadProgressHook progress_hook,
                                             DepotDownloadCancelHook cancel_hook,
                                             void* user_data) {
    g_download_progress_hook = progress_hook;
    g_download_cancel_hook = cancel_hook;
    g_download_hook_user_data = user_data;
}

void clear_current_thread_depot_download_hooks() {
    g_download_progress_hook = nullptr;
    g_download_cancel_hook = nullptr;
    g_download_hook_user_data = nullptr;
}

std::optional<cauth::core::depot::AppInfo> fetch_app_info_from_cm(std::uint64_t steam_id,
                                                                  std::uint32_t app_id,
                                                                  std::uint32_t max_count,
                                                                  std::ostream& out,
                                                                  std::ostream& err) {
    const auto store = cauth::core::platform::make_platform_session_repository();
    cauth::steam::auth::StoredSteamAuthProvider auth_provider{*store};
    cauth::core::depot::DepotCmClient depot_client{auth_provider, std::to_string(steam_id), &out, &err};
    return depot_client.fetch_app_info(app_id, max_count);
}

std::optional<cauth::core::depot::DepotDecryptionKeyResponse> fetch_depot_key_from_cm(
    std::uint64_t steam_id,
    std::uint32_t app_id,
    std::uint32_t depot_id,
    std::uint32_t max_count,
    std::ostream& out,
    std::ostream& err) {
    const auto store = cauth::core::platform::make_platform_session_repository();
    cauth::steam::auth::StoredSteamAuthProvider auth_provider{*store};
    cauth::core::depot::DepotCmClient depot_client{auth_provider, std::to_string(steam_id), &out, &err};
    return depot_client.fetch_depot_key(app_id, depot_id, max_count);
}

std::optional<cauth::core::depot::ManifestRequestCodeResponse>
fetch_manifest_request_code_from_cm(
    std::uint64_t steam_id,
    const cauth::core::depot::ManifestRequestCodeRequest& request,
    std::uint32_t max_count,
    std::ostream& out,
    std::ostream& err) {
    const auto store = cauth::core::platform::make_platform_session_repository();
    cauth::steam::auth::StoredSteamAuthProvider auth_provider{*store};
    cauth::core::depot::DepotCmClient depot_client{auth_provider, std::to_string(steam_id), &out, &err};
    return depot_client.fetch_manifest_request_code(request, max_count);
}

int print_branches(std::uint64_t steam_id,
                   std::uint32_t app_id,
                   std::uint32_t max_count,
                   std::ostream& out,
                   std::ostream& err) {
    const auto app_info = fetch_app_info_from_cm(steam_id, app_id, max_count, out, err);
    if (!app_info.has_value()) return 1;
    out << "Branches for app " << app_id << ": " << app_info->branches.size() << '\n';
    for (const auto& branch_info : app_info->branches) {
        out << "  branch=" << branch_info.name;
        if (!branch_info.build_id.empty()) out << " buildid=" << branch_info.build_id;
        if (branch_info.password_required) out << " password_required=true";
        out << '\n';
    }
    return 0;
}

int print_manifests(std::uint64_t steam_id,
                    std::uint32_t app_id,
                    std::string_view branch,
                    std::uint32_t max_count,
                    std::ostream& out,
                    std::ostream& err) {
    const auto app_info = fetch_app_info_from_cm(steam_id, app_id, max_count, out, err);
    if (!app_info.has_value()) return 1;
    out << "Depot manifests for app " << app_id << " branch " << branch << '\n';
    for (const auto& depot : app_info->depots) {
        for (const auto& manifest : depot.manifests) {
            if (manifest.branch != branch) continue;
            out << "  depot=" << depot.depot_id << " gid=" << manifest.manifest_gid;
            if (manifest.encrypted) out << " encrypted=true";
            append_depot_platform_summary(out, depot);
            out << '\n';
        }
    }
    return 0;
}

int print_preflight(std::uint64_t steam_id,
                    std::uint32_t app_id,
                    std::string_view branch,
                    std::uint32_t max_count,
                    std::ostream& out,
                    std::ostream& err) {
    const auto app_info = fetch_app_info_from_cm(steam_id, app_id, max_count, out, err);
    if (!app_info.has_value()) return 1;
    out << "Preflight for app " << app_id << " branch " << branch << '\n';
    for (const auto& depot : app_info->depots) {
        for (const auto& manifest : depot.manifests) {
            if (manifest.branch != branch) continue;
            out << "  depot=" << depot.depot_id << " manifest=" << manifest.manifest_gid;
            if (manifest.encrypted) out << " encrypted=true";
            append_depot_platform_summary(out, depot);
            out << '\n';
        }
    }
    return 0;
}

int print_depot_key(std::uint64_t steam_id,
                    std::uint32_t app_id,
                    std::uint32_t depot_id,
                    std::uint32_t max_count,
                    std::ostream& out,
                    std::ostream& err) {
    const auto response = fetch_depot_key_from_cm(steam_id, app_id, depot_id, max_count, out, err);
    if (!response.has_value()) return 1;
    out << "Depot key for app " << app_id << " depot " << depot_id << ": eresult=" << response->eresult;
    if (response->depot_id != 0) out << " response_depot=" << response->depot_id;
    if (!response->key.empty()) out << " key=" << cauth::core::cm::bytes_to_hex(response->key);
    out << '\n';
    return response->eresult == 1 ? 0 : 1;
}

int print_manifest_request_code(std::uint64_t steam_id,
                                std::uint32_t app_id,
                                std::uint32_t depot_id,
                                std::uint64_t manifest_gid,
                                std::string_view branch,
                                std::uint32_t max_count,
                                std::ostream& out,
                                std::ostream& err) {
    const auto response = fetch_manifest_request_code_from_cm(
        steam_id,
        cauth::core::depot::ManifestRequestCodeRequest{
            app_id,
            depot_id,
            manifest_gid,
            std::string{branch},
            {},
        },
        max_count,
        out,
        err);
    if (!response.has_value()) return 1;
    out << "Manifest request code for app " << app_id
        << " depot " << depot_id
        << " gid " << manifest_gid
        << " branch " << branch
        << ": " << response->manifest_request_code << '\n';
    return response->manifest_request_code == 0 ? 1 : 0;
}

bool load_manifest_from_path(const std::string& input_path,
                             const std::optional<std::vector<std::uint8_t>>& depot_key,
                             LoadedDepotManifest& loaded_manifest,
                             std::ostream& err) {
    std::ifstream input{input_path, std::ios::binary | std::ios::ate};
    if (!input) { err << "Failed to open manifest path: " << input_path << '\n'; return false; }
    const auto size = input.tellg();
    if (size < 0) { err << "Failed to read manifest size: " << input_path << '\n'; return false; }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!input && !bytes.empty()) { err << "Failed to read manifest path: " << input_path << '\n'; return false; }
    if (bytes.empty()) {
        err << "Manifest file is empty: " << input_path << '\n';
        return false;
    }

    auto parsed = cauth::core::depot::parse_depot_manifest(bytes);
    if (!parsed.ok) {
        err << "Manifest parse failed: " << parsed.error_message
            << " path=" << input_path
            << " size=" << bytes.size()
            << " prefix_hex=";
        const auto prefix_size = std::min<std::size_t>(bytes.size(), 16);
        err << std::hex << std::setfill('0');
        for (std::size_t index = 0; index < prefix_size; ++index) {
            err << std::setw(2) << static_cast<unsigned int>(bytes[index]);
            if (index + 1 < prefix_size) {
                err << ' ';
            }
        }
        err << std::dec;
        if (bytes.size() >= 4) {
            const bool looks_like_html =
                (bytes[0] == '<' && (bytes[1] == '!' || bytes[1] == 'h' || bytes[1] == 'H')) ||
                (bytes[0] == '\r' || bytes[0] == '\n' || bytes[0] == ' ');
            if (looks_like_html) {
                err << " hint=looks_like_text_or_html";
            } else if (bytes[0] == 'P' && bytes[1] == 'K') {
                err << " hint=zip_like_payload";
            }
        }
        err << '\n';
        return false;
    }

    loaded_manifest.manifest = std::move(parsed.manifest);
    loaded_manifest.depot_key = depot_key;
    if (loaded_manifest.depot_key.has_value()) {
        const auto decrypt_result = cauth::core::depot::decrypt_depot_manifest_filenames(
            loaded_manifest.manifest,
            *loaded_manifest.depot_key);
        if (!decrypt_result.ok) {
            err << "Manifest filename decrypt failed: " << decrypt_result.error_message << '\n';
            return false;
        }
    }
    return true;
}

int print_manifest_info(const LoadedDepotManifest& loaded_manifest, std::ostream& out) {
    const auto& manifest = loaded_manifest.manifest;
    std::uint64_t chunk_count = 0;
    for (const auto& file : manifest.files) chunk_count += file.chunks.size();
    out << "Manifest info\n"
        << "  depot_id=" << manifest.depot_id << '\n'
        << "  manifest_gid=" << manifest.manifest_gid << '\n'
        << "  creation_time=" << manifest.creation_time << '\n'
        << "  filenames_encrypted=" << (manifest.filenames_encrypted ? "true" : "false") << '\n'
        << "  files=" << manifest.files.size() << '\n'
        << "  chunks=" << chunk_count << '\n'
        << "  total_uncompressed=" << manifest.total_uncompressed_size << '\n'
        << "  total_compressed=" << manifest.total_compressed_size << '\n';
    const auto preview_count = std::min<std::size_t>(manifest.files.size(), 5);
    for (std::size_t index = 0; index < preview_count; ++index) {
        const auto& file = manifest.files[index];
        out << "  file[" << index << "]=" << file.filename
            << " size=" << file.size
            << " chunks=" << file.chunks.size() << '\n';
    }
    return 0;
}

int print_file_list(const LoadedDepotManifest& loaded_manifest,
                    std::string_view filter_text,
                    std::size_t list_limit,
                    std::ostream& out) {
    const auto& manifest = loaded_manifest.manifest;
    std::size_t matched = 0;
    std::size_t printed = 0;
    for (std::size_t index = 0; index < manifest.files.size(); ++index) {
        const auto& file = manifest.files[index];
        if (cauth::core::depot::depot_file_is_directory(file)) continue;
        if (!contains_ascii_case_insensitive(file.filename, filter_text)) continue;
        ++matched;
        if (printed >= list_limit) continue;
        out << "file[" << index << "]=" << file.filename
            << " size=" << file.size
            << " chunks=" << file.chunks.size() << '\n';
        ++printed;
    }
    out << "File list: matched=" << matched
        << " printed=" << printed
        << " total=" << count_non_directory_manifest_files(manifest) << '\n';
    return 0;
}

int verify_local_files_against_manifest(const LoadedDepotManifest& loaded_manifest,
                                        const std::string& local_root,
                                        std::string_view filter_text,
                                        std::ostream& out,
                                        std::ostream& err,
                                        LocalVerifyReport* report) {
    if (local_root.empty()) {
        err << "Local root is required\n";
        if (report != nullptr) {
            report->fatal_error = true;
        }
        return 1;
    }

    const auto& manifest = loaded_manifest.manifest;
    std::uint64_t checked = 0;
    std::uint64_t matched = 0;
    std::uint64_t missing = 0;
    std::uint64_t mismatched = 0;
    std::uint64_t size_only = 0;
    std::uint64_t filtered_out = 0;

    if (report != nullptr) {
        *report = LocalVerifyReport{};
        report->total_count = count_non_directory_manifest_files(manifest);
    }

    std::error_code root_error;
    const std::filesystem::path root_path{local_root};
    if (!std::filesystem::exists(root_path, root_error)) {
        err << "Local root does not exist: " << local_root;
        if (root_error) {
            err << " (" << root_error.message() << ")";
        }
        err << '\n';
        if (report != nullptr) {
            report->fatal_error = true;
        }
        return 1;
    }
    if (!std::filesystem::is_directory(root_path, root_error)) {
        err << "Local root is not a directory: " << local_root;
        if (root_error) {
            err << " (" << root_error.message() << ")";
        }
        err << '\n';
        if (report != nullptr) {
            report->fatal_error = true;
        }
        return 1;
    }
    if (manifest.filenames_encrypted && !loaded_manifest.depot_key.has_value()) {
        err << "Depot key is required for local verify because manifest filenames are encrypted\n";
        if (report != nullptr) {
            report->fatal_error = true;
        }
        return 1;
    }

    for (const auto& file : manifest.files) {
        if (cauth::core::depot::depot_file_is_directory(file)) {
            continue;
        }
        if (!contains_ascii_case_insensitive(file.filename, filter_text)) {
            ++filtered_out;
            continue;
        }
        ++checked;

        const auto output_path = make_safe_manifest_output_path(local_root, file.filename, err);
        if (!output_path.has_value()) {
            ++mismatched;
            continue;
        }

        const auto verify_result =
            cauth::core::depot::verify_depot_file_on_disk(*output_path, file);
        if (verify_result.ok) {
            ++matched;
            if (!cauth::core::depot::depot_file_has_binary_verification(file)) {
                ++size_only;
                out << "SIZE_ONLY file=" << file.filename
                    << " path=" << output_path->string() << '\n';
            } else {
                out << "OK file=" << file.filename
                    << " path=" << output_path->string() << '\n';
            }
            continue;
        }

        if (verify_result.error_message.find("missing") != std::string::npos) {
            ++missing;
            out << "MISSING file=" << file.filename
                << " path=" << output_path->string() << '\n';
            continue;
        }

        ++mismatched;
        out << "MISMATCH file=" << file.filename
            << " path=" << output_path->string()
            << " reason=" << verify_result.error_message << '\n';
    }

    if (report != nullptr) {
        report->checked_count = checked;
        report->ok_count = matched;
        report->missing_count = missing;
        report->mismatched_count = mismatched;
        report->size_only_count = size_only;
        report->filtered_out_count = filtered_out;
    }

    out << "Local verify: checked=" << checked
        << " ok=" << matched
        << " missing=" << missing
        << " mismatched=" << mismatched
        << " size_only=" << size_only
        << " filtered_out=" << filtered_out
        << " total=" << count_non_directory_manifest_files(manifest)
        << " local_root=" << local_root << '\n';
    return (missing == 0 && mismatched == 0) ? 0 : 1;
}

std::optional<std::size_t> resolve_file_selection(const LoadedDepotManifest& loaded_manifest,
                                                  std::size_t file_index,
                                                  bool has_file_index,
                                                  std::string_view file_path,
                                                  std::ostream& err) {
    const auto& manifest = loaded_manifest.manifest;
    if (!file_path.empty()) {
        const auto matched_file_index = cauth::core::depot::find_depot_file_index(manifest, file_path);
        if (!matched_file_index.has_value()) {
            err << "file not found in manifest: " << file_path << '\n';
            return std::nullopt;
        }
        if (cauth::core::depot::depot_file_is_directory(manifest.files[*matched_file_index])) {
            err << "manifest entry is a directory: " << file_path << '\n';
            return std::nullopt;
        }
        return *matched_file_index;
    }
    if (!has_file_index || file_index >= manifest.files.size()) {
        err << "file-index out of range: " << file_index << '\n';
        return std::nullopt;
    }
    if (cauth::core::depot::depot_file_is_directory(manifest.files[file_index])) {
        err << "manifest entry is a directory: file-index " << file_index << '\n';
        return std::nullopt;
    }
    return file_index;
}

bool validate_chunk_selection(const LoadedDepotManifest& loaded_manifest,
                              std::size_t file_index,
                              std::size_t chunk_index,
                              std::ostream& err) {
    const auto& file = loaded_manifest.manifest.files[file_index];
    if (chunk_index < file.chunks.size()) return true;
    err << "chunk-index out of range: " << chunk_index << '\n';
    return false;
}

int download_manifest_to_path(std::uint32_t depot_id,
                              std::uint64_t manifest_gid,
                              std::uint64_t request_code,
                              std::uint32_t max_count,
                              const std::string& output_path,
                              std::ostream& out,
                              std::ostream& err) {
    const auto cdn_servers = fetch_cdn_servers_for_download(max_count, err);
    if (!cdn_servers.has_value()) {
        return 1;
    }

    cauth::core::depot::ManifestDownloader downloader;
    const cauth::core::depot::ManifestDownloadRequest download_request{
        depot_id,
        manifest_gid,
        request_code,
    };
    const auto total_servers = static_cast<std::uint64_t>(cdn_servers->size());
    for (std::size_t server_index = 0; server_index < cdn_servers->size(); ++server_index) {
        if (fail_if_download_canceled(err)) {
            return 1;
        }
        const auto& server = (*cdn_servers)[server_index];
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::Manifest,
            "Requesting manifest from " + server.vhost,
            output_path,
            static_cast<std::uint64_t>(server_index),
            total_servers,
            0,
            0,
        });
        out << "Downloading manifest from " << server.vhost << "...\n";
        const auto result = downloader.download_raw_manifest(server, download_request);
        if (!result.ok) {
            err << "Manifest download failed from " << result.url << ": " << result.error_message << '\n';
            continue;
        }
        if (fail_if_download_canceled(err)) {
            return 1;
        }
        if (!ensure_parent_directory_exists(output_path, err)) {
            return 1;
        }
        std::ofstream output{output_path, std::ios::binary};
        if (!output) { err << "Failed to open output path: " << output_path << '\n'; return 1; }
        output.write(reinterpret_cast<const char*>(result.bytes.data()),
                     static_cast<std::streamsize>(result.bytes.size()));
        if (!output) { err << "Failed to write output path: " << output_path << '\n'; return 1; }
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::Manifest,
            "Manifest downloaded",
            output_path,
            total_servers,
            total_servers,
            static_cast<std::uint64_t>(result.bytes.size()),
            static_cast<std::uint64_t>(result.bytes.size()),
        });
        out << "Manifest downloaded: " << result.bytes.size() << " bytes -> " << output_path << '\n';
        return 0;
    }

    err << "Manifest download failed for all CDN servers\n";
    return 1;
}

int download_chunk_from_manifest(const LoadedDepotManifest& loaded_manifest,
                                 std::size_t file_index,
                                 std::size_t chunk_index,
                                 bool process_chunk,
                                 std::uint32_t max_count,
                                 const std::string& output_path,
                                 std::ostream& out,
                                 std::ostream& err) {
    const auto& file = loaded_manifest.manifest.files[file_index];
    const auto cdn_servers = fetch_cdn_servers_for_download(max_count, err);
    if (!cdn_servers.has_value()) return 1;

    cauth::core::depot::ManifestDownloader downloader;
    const auto& selected_chunk = file.chunks[chunk_index];
    const auto total_servers = static_cast<std::uint64_t>(cdn_servers->size());
    report_download_progress(DepotDownloadProgress{
        DepotDownloadKind::Chunk,
        "Preparing chunk download",
        file.filename,
        0,
        1,
        0,
        process_chunk ? selected_chunk.uncompressed_size : selected_chunk.compressed_size,
    });
    for (std::size_t server_index = 0; server_index < cdn_servers->size(); ++server_index) {
        if (fail_if_download_canceled(err)) {
            return 1;
        }
        const auto& server = (*cdn_servers)[server_index];
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::Chunk,
            "Downloading chunk from " + server.vhost,
            file.filename,
            static_cast<std::uint64_t>(server_index),
            total_servers,
            0,
            process_chunk ? selected_chunk.uncompressed_size : selected_chunk.compressed_size,
        });
        out << "Downloading file chunk " << (chunk_index + 1) << '/' << file.chunks.size()
            << " from " << server.vhost << "...\n";
        const auto response = downloader.download_raw_chunk(
            server,
            cauth::core::depot::ChunkDownloadRequest{loaded_manifest.manifest.depot_id, selected_chunk.sha});
        if (!response.ok) {
            err << "Chunk download failed from " << response.url << ": "
                << response.error_message << '\n';
            continue;
        }
        if (fail_if_download_canceled(err)) {
            return 1;
        }

        std::vector<std::uint8_t> output_bytes = response.bytes;
        if (process_chunk) {
            if (!loaded_manifest.depot_key.has_value()) {
                err << "Chunk process failed: depot key is required\n";
                return 1;
            }
            const auto processed = cauth::core::depot::process_depot_chunk(
                selected_chunk,
                response.bytes,
                *loaded_manifest.depot_key);
            if (!processed.ok) { err << "Chunk process failed: " << processed.error_message << '\n'; continue; }
            output_bytes = std::move(processed.bytes);
        }
        if (fail_if_download_canceled(err)) {
            return 1;
        }

        if (!ensure_parent_directory_exists(output_path, err)) {
            return 1;
        }
        std::ofstream output{output_path, std::ios::binary};
        if (!output) { err << "Failed to open output path: " << output_path << '\n'; return 1; }
        output.write(reinterpret_cast<const char*>(output_bytes.data()),
                     static_cast<std::streamsize>(output_bytes.size()));
        if (!output) { err << "Failed to write output path: " << output_path << '\n'; return 1; }
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::Chunk,
            "Chunk downloaded",
            file.filename,
            1,
            1,
            static_cast<std::uint64_t>(output_bytes.size()),
            process_chunk ? selected_chunk.uncompressed_size : selected_chunk.compressed_size,
        });
        out << "Chunk downloaded: " << output_bytes.size() << " bytes -> " << output_path << '\n';
        return 0;
    }

    err << "Chunk download failed for all CDN servers\n";
    return 1;
}

int download_file_from_manifest(const LoadedDepotManifest& loaded_manifest,
                                std::size_t file_index,
                                std::uint32_t max_count,
                                const std::string& output_path,
                                std::ostream& out,
                                std::ostream& err) {
    if (!loaded_manifest.depot_key.has_value()) {
        err << "File download failed: depot key is required\n";
        return 1;
    }

    const auto& file = loaded_manifest.manifest.files[file_index];
    const auto cdn_servers = fetch_cdn_servers_for_download(max_count, err);
    if (!cdn_servers.has_value()) return 1;

    if (!ensure_parent_directory_exists(output_path, err)) {
        return 1;
    }
    std::ofstream output{output_path, std::ios::binary};
    if (!output) { err << "Failed to open output path: " << output_path << '\n'; return 1; }

    cauth::core::depot::ManifestDownloader downloader;
    std::uint64_t completed_bytes = 0;
    report_download_progress(DepotDownloadProgress{
        DepotDownloadKind::File,
        "Preparing file download",
        file.filename,
        0,
        static_cast<std::uint64_t>(file.chunks.size()),
        0,
        file.size,
    });
    for (std::size_t current_chunk_index = 0; current_chunk_index < file.chunks.size(); ++current_chunk_index) {
        if (fail_if_download_canceled(err)) {
            return 1;
        }
        bool chunk_written = false;
        for (const auto& server : *cdn_servers) {
            if (fail_if_download_canceled(err)) {
                return 1;
            }
            report_download_progress(DepotDownloadProgress{
                DepotDownloadKind::File,
                "Downloading chunk " + std::to_string(current_chunk_index + 1) + "/" +
                    std::to_string(file.chunks.size()) + " from " + server.vhost,
                file.filename,
                static_cast<std::uint64_t>(current_chunk_index),
                static_cast<std::uint64_t>(file.chunks.size()),
                completed_bytes,
                file.size,
            });
            out << "Downloading file chunk " << (current_chunk_index + 1) << '/' << file.chunks.size()
                << " from " << server.vhost << "...\n";
            const auto response = downloader.download_raw_chunk(
                server,
                cauth::core::depot::ChunkDownloadRequest{
                    loaded_manifest.manifest.depot_id,
                    file.chunks[current_chunk_index].sha,
                });
            if (!response.ok) {
                err << "Chunk download failed from " << response.url << ": "
                    << response.error_message << '\n';
                continue;
            }
            if (fail_if_download_canceled(err)) {
                return 1;
            }
            const auto processed = cauth::core::depot::process_depot_chunk(
                file.chunks[current_chunk_index],
                response.bytes,
                *loaded_manifest.depot_key);
            if (!processed.ok) { err << "Chunk process failed: " << processed.error_message << '\n'; continue; }
            if (fail_if_download_canceled(err)) {
                return 1;
            }
            output.write(reinterpret_cast<const char*>(processed.bytes.data()),
                         static_cast<std::streamsize>(processed.bytes.size()));
            if (!output) { err << "Failed to write output path: " << output_path << '\n'; return 1; }
            completed_bytes += static_cast<std::uint64_t>(processed.bytes.size());
            report_download_progress(DepotDownloadProgress{
                DepotDownloadKind::File,
                "Downloaded chunk " + std::to_string(current_chunk_index + 1) + "/" +
                    std::to_string(file.chunks.size()),
                file.filename,
                static_cast<std::uint64_t>(current_chunk_index + 1),
                static_cast<std::uint64_t>(file.chunks.size()),
                completed_bytes,
                file.size,
            });
            chunk_written = true;
            break;
        }
        if (!chunk_written) {
            err << "File download failed at chunk " << current_chunk_index << '\n';
            return 1;
        }
    }

    report_download_progress(DepotDownloadProgress{
        DepotDownloadKind::File,
        "File downloaded",
        file.filename,
        static_cast<std::uint64_t>(file.chunks.size()),
        static_cast<std::uint64_t>(file.chunks.size()),
        completed_bytes,
        file.size,
    });
    out << "File downloaded: " << file.filename << " -> " << output_path << '\n';
    return 0;
}

int download_all_files_from_manifest(const LoadedDepotManifest& loaded_manifest,
                                     std::uint32_t max_count,
                                     const std::string& output_root,
                                     std::ostream& out,
                                     std::ostream& err) {
    if (!loaded_manifest.depot_key.has_value()) {
        err << "All-files download failed: depot key is required\n";
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path{output_root}, ec);
    if (ec && !std::filesystem::exists(std::filesystem::path{output_root})) {
        err << "Failed to create output root: " << output_root << " (" << ec.message() << ")\n";
        return 1;
    }

    const auto& files = loaded_manifest.manifest.files;
    const auto total_files = count_non_directory_manifest_files(loaded_manifest.manifest);
    const auto total_bytes = loaded_manifest.manifest.total_uncompressed_size;
    std::uint64_t completed_bytes = 0;

    report_download_progress(DepotDownloadProgress{
        DepotDownloadKind::AllFiles,
        "Preparing file batch download",
        output_root,
        0,
        total_files,
        0,
        total_bytes,
    });

    for (std::size_t file_index = 0; file_index < files.size(); ++file_index) {
        if (fail_if_download_canceled(err)) {
            return 1;
        }
        const auto& file = files[file_index];
        if (cauth::core::depot::depot_file_is_directory(file)) {
            if (!ensure_manifest_directory_exists(output_root, file.filename, err)) {
                return 1;
            }
            out << "Created manifest directory: " << file.filename << '\n';
            continue;
        }
        const auto output_path = make_safe_manifest_output_path(output_root, file.filename, err);
        if (!output_path.has_value()) {
            return 1;
        }

        const auto completed_files_before = static_cast<std::uint64_t>(file_index);
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::AllFiles,
            "Downloading file " + std::to_string(file_index + 1) + "/" +
                std::to_string(files.size()),
            file.filename,
            completed_files_before,
            total_files,
            completed_bytes,
            total_bytes,
        });
        out << "Downloading manifest file " << (file_index + 1) << '/' << files.size()
            << ": " << file.filename << " -> " << output_path->string() << '\n';

        const auto exit_code = download_file_from_manifest(
            loaded_manifest,
            file_index,
            max_count,
            output_path->string(),
            out,
            err);
        if (exit_code != 0) {
            err << "All-files download failed at file " << file.filename << '\n';
            return exit_code;
        }

        completed_bytes += file.size;
        std::uint64_t completed_files = 0;
        for (std::size_t completed_index = 0; completed_index <= file_index; ++completed_index) {
            if (!cauth::core::depot::depot_file_is_directory(files[completed_index])) {
                ++completed_files;
            }
        }
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::AllFiles,
            "Downloaded file " + std::to_string(file_index + 1) + "/" +
                std::to_string(files.size()),
            file.filename,
            completed_files,
            total_files,
            completed_bytes,
            total_bytes,
        });
    }

    report_download_progress(DepotDownloadProgress{
        DepotDownloadKind::AllFiles,
        "All files downloaded",
        output_root,
        total_files,
        total_files,
        completed_bytes,
        total_bytes,
    });
    out << "All files downloaded: " << files.size() << " file(s) -> " << output_root << '\n';
    return 0;
}

} // namespace cauth::steam::depot
