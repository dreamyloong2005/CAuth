#include "steam/depot/steam_depot_application.hpp"

#include "core/platform/endpoint_route_cache.hpp"
#include "core/platform/session_repository_factory.hpp"
#include "steam/cm/cm_message.hpp"
#include "steam/auth/steam_auth_provider.hpp"
#include "steam/depot/cdn_directory.hpp"
#include "steam/depot/depot_chunk.hpp"
#include "steam/depot/depot_file.hpp"
#include "steam/depot/depot_cm_client.hpp"
#include "steam/depot/manifest_downloader.hpp"

#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace cauth::steam::depot {
namespace {

thread_local DepotDownloadProgressHook g_download_progress_hook = nullptr;
thread_local DepotDownloadCancelHook g_download_cancel_hook = nullptr;
thread_local DepotDownloadPauseHook g_download_pause_hook = nullptr;
thread_local void* g_download_hook_user_data = nullptr;

enum class DownloadInterruptAction {
    None,
    Pause,
    Cancel,
};

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

std::string infer_depot_module_status(DepotDownloadKind kind, std::string_view phase) {
    if (contains_ascii_case_insensitive(phase, "cancel")) {
        return "canceled";
    }
    if (contains_ascii_case_insensitive(phase, "prepare")) {
        return "preparing";
    }
    if (contains_ascii_case_insensitive(phase, "list")) {
        return "listing";
    }
    if (kind == DepotDownloadKind::VerifyLocal ||
        contains_ascii_case_insensitive(phase, "verify")) {
        return "verifying";
    }
    return "downloading";
}

std::string trim_trailing_line_breaks(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

std::uint64_t read_file_size_or_zero(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : size;
}

std::optional<std::vector<std::uint8_t>> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        return std::nullopt;
    }
    return bytes;
}

LocalVerifyEntry make_local_verify_entry_base(const cauth::core::depot::DepotManifestFile& file,
                                              std::string_view display_name) {
    LocalVerifyEntry entry;
    entry.manifest_filename = std::string{display_name};
    entry.expected_size = file.size;
    if (!file.content_sha.empty()) {
        entry.expected_sha_hex = cauth::core::cm::bytes_to_hex(file.content_sha);
    }
    return entry;
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

std::string display_manifest_filename(std::string_view manifest_filename) {
    return cauth::core::depot::depot_manifest_path_for_display(manifest_filename);
}

std::optional<std::filesystem::path> make_safe_manifest_output_path(
    const std::string& output_root,
    std::string_view manifest_filename,
    std::ostream& err) {
    if (output_root.empty()) {
        err << "Output root is required\n";
        return std::nullopt;
    }

    const auto normalized = cauth::core::depot::normalize_depot_manifest_path(manifest_filename);
    if (!normalized.ok) {
        err << "Unsafe manifest file path: " << manifest_filename
            << " (" << normalized.error_message << ")\n";
        return std::nullopt;
    }
    return std::filesystem::path{output_root} / normalized.relative_path;
}

DownloadInterruptAction current_download_interrupt_action() {
    if (g_download_cancel_hook != nullptr && g_download_cancel_hook(g_download_hook_user_data)) {
        return DownloadInterruptAction::Cancel;
    }
    if (g_download_pause_hook != nullptr && g_download_pause_hook(g_download_hook_user_data)) {
        return DownloadInterruptAction::Pause;
    }
    return DownloadInterruptAction::None;
}

bool is_download_interrupted() {
    return current_download_interrupt_action() != DownloadInterruptAction::None;
}

bool fail_if_download_interrupted(std::ostream& err) {
    const auto action = current_download_interrupt_action();
    if (action == DownloadInterruptAction::None) {
        return false;
    }
    err << (action == DownloadInterruptAction::Pause ? "operation paused\n"
                                                     : "operation canceled\n");
    return true;
}

void report_download_progress(DepotDownloadProgress progress) {
    if (g_download_progress_hook == nullptr) {
        return;
    }
    if (progress.module_status.empty() || progress.module_status == "idle") {
        progress.module_status = infer_depot_module_status(progress.kind, progress.phase);
    }
    g_download_progress_hook(progress, g_download_hook_user_data);
}

std::string cdn_server_route_key(const cauth::core::depot::CdnServer& server) {
    return std::string{server.protocol == cauth::core::depot::CdnServerProtocol::Https ? "https://" : "http://"} +
           (server.vhost.empty() ? server.host : server.vhost) + ":" + std::to_string(server.port);
}

std::string build_cdn_probe_url(const cauth::core::depot::CdnServer& server) {
    std::string url =
        std::string{server.protocol == cauth::core::depot::CdnServerProtocol::Https ? "https://" : "http://"} +
        (server.vhost.empty() ? server.host : server.vhost);
    if ((server.protocol == cauth::core::depot::CdnServerProtocol::Https && server.port != 443) ||
        (server.protocol == cauth::core::depot::CdnServerProtocol::Http && server.port != 80)) {
        url += ":" + std::to_string(server.port);
    }
    url += "/";
    return url;
}

cauth::core::platform::EndpointProbeOutcome probe_cdn_server_latency(
    const cauth::core::depot::CdnServer& server) {
    return cauth::core::platform::probe_http_endpoint(build_cdn_probe_url(server), 2000, 2000);
}

void record_cdn_attempt_result(const cauth::core::depot::CdnServer& server,
                               bool ok,
                               std::chrono::steady_clock::duration elapsed) {
    auto& cache = cauth::core::platform::EndpointRouteCache::instance();
    const auto route_key = cdn_server_route_key(server);
    if (ok) {
        cache.record_success(
            "steam.depot.cdn",
            route_key,
            cauth::core::platform::elapsed_milliseconds(elapsed));
        return;
    }
    cache.record_failure("steam.depot.cdn", route_key);
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
    const cauth::core::platform::RouteSelection* route_selection,
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
    prepared = cauth::core::platform::rank_endpoints_by_route_health(
        "steam.depot.cdn",
        std::move(prepared),
        [](const cauth::core::depot::CdnServer& server) { return cdn_server_route_key(server); },
        [](const cauth::core::depot::CdnServer& server) { return probe_cdn_server_latency(server); },
        6);
    if (route_selection != nullptr && !route_selection->empty()) {
        std::vector<cauth::core::depot::CdnServer> filtered;
        filtered.reserve(prepared.size());
        for (auto& server : prepared) {
            if (cauth::core::platform::route_selection_matches(
                    route_selection,
                    (server.vhost.empty() ? server.host : server.vhost) + ":" + std::to_string(server.port),
                    server.protocol == cauth::core::depot::CdnServerProtocol::Https ? "https" : "http")) {
                filtered.push_back(std::move(server));
            }
        }
        if (filtered.empty()) {
            err << "selected depot download route is not available: "
                << route_selection->endpoint << '\n';
            return std::nullopt;
        }
        prepared = std::move(filtered);
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

std::uint64_t scale_http_progress_bytes(std::uint64_t transferred_bytes,
                                        std::uint64_t http_total_bytes,
                                        std::uint64_t logical_total_bytes) {
    if (logical_total_bytes == 0) {
        return transferred_bytes;
    }
    const auto denominator = http_total_bytes != 0 ? http_total_bytes : logical_total_bytes;
    if (denominator == 0) {
        return transferred_bytes;
    }
    if (transferred_bytes >= denominator) {
        return logical_total_bytes;
    }
    return (logical_total_bytes * transferred_bytes) / denominator;
}

struct DepotHttpProgressContext {
    DepotDownloadKind kind = DepotDownloadKind::Manifest;
    std::string phase;
    std::string target;
    std::uint64_t completed_steps = 0;
    std::uint64_t total_steps = 0;
    std::uint64_t completed_bytes_base = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t logical_current_bytes = 0;
};

void report_depot_http_progress(const cauth::core::platform::HttpTransferProgress& progress,
                                void* user_data) {
    if (progress.direction != cauth::core::platform::HttpTransferDirection::Download) {
        return;
    }

    const auto* context = static_cast<const DepotHttpProgressContext*>(user_data);
    if (context == nullptr) {
        return;
    }

    const auto current_bytes = scale_http_progress_bytes(
        progress.bytes_transferred, progress.total_bytes, context->logical_current_bytes);
    report_download_progress(DepotDownloadProgress{
        context->kind,
        context->phase,
        context->target,
        context->completed_steps,
        context->total_steps,
        context->completed_bytes_base + current_bytes,
        context->total_bytes != 0 ? context->total_bytes
                                  : context->completed_bytes_base + progress.total_bytes,
    });
}

bool is_depot_http_download_interrupted(void*) { return is_download_interrupted(); }

struct DepotFileDownloadOutcome {
    int exit_code = 0;
    bool skipped = false;
    std::uint64_t written_bytes = 0;
};

struct StreamedDepotWriteContext {
    cauth::core::platform::PreparedFileWrite* prepared_output = nullptr;
    std::ofstream* output = nullptr;
    std::string error_message;
    std::uint64_t committed_bytes = 0;
};

struct StreamedDepotDownloadOutcome {
    bool ok = false;
    bool paused = false;
    bool canceled = false;
    std::uint64_t written_bytes = 0;
    std::string error_message;
};

struct DepotBatchResumeState {
    std::string token;
    std::size_t next_file_index = 0;
    std::uint64_t completed_bytes = 0;
};

std::string make_manifest_resume_token(std::uint32_t depot_id,
                                       std::uint64_t manifest_gid,
                                       std::uint64_t request_code) {
    return "depot-manifest-v1:" + std::to_string(depot_id) + ":" +
           std::to_string(manifest_gid) + ":" + std::to_string(request_code);
}

std::string make_chunk_resume_token(std::uint32_t depot_id,
                                    std::string_view chunk_sha_hex,
                                    bool processed_chunk) {
    return std::string{"depot-chunk-v1:"} + std::to_string(depot_id) + ":" +
           std::string{chunk_sha_hex} + ":" + (processed_chunk ? "processed" : "raw");
}

std::string make_file_resume_token(const LoadedDepotManifest& loaded_manifest,
                                   const cauth::core::depot::DepotManifestFile& file) {
    return std::string{"depot-file-v1:"} +
           std::to_string(loaded_manifest.manifest.depot_id) + ":" +
           std::to_string(loaded_manifest.manifest.manifest_gid) + ":" +
           display_manifest_filename(file.filename) + ":" + std::to_string(file.size);
}

std::string make_all_files_resume_token(const LoadedDepotManifest& loaded_manifest,
                                        std::string_view output_root) {
    return std::string{"depot-all-files-v1:"} +
           std::to_string(loaded_manifest.manifest.depot_id) + ":" +
           std::to_string(loaded_manifest.manifest.manifest_gid) + ":" +
           std::string{output_root};
}

std::filesystem::path make_all_files_resume_state_path(std::string_view output_root) {
    return std::filesystem::path{std::string{output_root}} / ".cauthdownload.depot.batch.resume";
}

bool load_all_files_resume_state(const std::filesystem::path& state_path,
                                 DepotBatchResumeState& state,
                                 std::string& error_message) {
    std::ifstream input(state_path, std::ios::binary);
    if (!input) {
        error_message = "Failed to open batch resume state: " + state_path.string();
        return false;
    }

    std::string version_line;
    std::string next_file_line;
    std::string bytes_line;
    std::string token_line;
    if (!std::getline(input, version_line) || !std::getline(input, next_file_line) ||
        !std::getline(input, bytes_line) || !std::getline(input, token_line)) {
        error_message = "Batch resume state is truncated: " + state_path.string();
        return false;
    }
    if (version_line != "cauth-depot-batch-v1") {
        error_message = "Batch resume state version is unsupported: " + state_path.string();
        return false;
    }
    if (next_file_line.rfind("next_file=", 0) != 0 || bytes_line.rfind("completed_bytes=", 0) != 0 ||
        token_line.rfind("token=", 0) != 0) {
        error_message = "Batch resume state is invalid: " + state_path.string();
        return false;
    }
    try {
        state.next_file_index = static_cast<std::size_t>(std::stoull(next_file_line.substr(10)));
        state.completed_bytes = std::stoull(bytes_line.substr(16));
    } catch (...) {
        error_message = "Batch resume state counters are invalid: " + state_path.string();
        return false;
    }
    state.token = token_line.substr(6);
    return true;
}

bool save_all_files_resume_state(const std::filesystem::path& state_path,
                                 const DepotBatchResumeState& state,
                                 std::string& error_message) {
    std::ofstream output(state_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error_message = "Failed to open batch resume state for writing: " + state_path.string();
        return false;
    }
    output << "cauth-depot-batch-v1\n";
    output << "next_file=" << state.next_file_index << "\n";
    output << "completed_bytes=" << state.completed_bytes << "\n";
    output << "token=" << state.token << "\n";
    output.close();
    if (!output) {
        error_message = "Failed to finalize batch resume state: " + state_path.string();
        return false;
    }
    return true;
}

bool clear_all_files_resume_state(const std::filesystem::path& state_path, std::string& error_message) {
    std::error_code ec;
    const auto exists = std::filesystem::exists(state_path, ec);
    if (ec) {
        error_message = "Failed to inspect batch resume state: " + state_path.string();
        return false;
    }
    if (!exists) {
        return true;
    }
    std::filesystem::remove(state_path, ec);
    if (ec) {
        error_message = "Failed to remove batch resume state: " + state_path.string();
        return false;
    }
    return true;
}

std::optional<std::size_t> resolve_resume_chunk_index(
    const cauth::core::depot::DepotManifestFile& file,
    std::uint64_t resume_offset) {
    std::uint64_t offset = 0;
    for (std::size_t index = 0; index < file.chunks.size(); ++index) {
        if (offset == resume_offset) {
            return index;
        }
        offset += file.chunks[index].uncompressed_size;
        if (resume_offset < offset) {
            return std::nullopt;
        }
    }
    if (offset == resume_offset) {
        return file.chunks.size();
    }
    return std::nullopt;
}

bool write_streamed_depot_bytes(const std::uint8_t* bytes, std::size_t size, void* user_data) {
    auto* context = static_cast<StreamedDepotWriteContext*>(user_data);
    if (context == nullptr || context->prepared_output == nullptr || context->output == nullptr) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    context->output->write(reinterpret_cast<const char*>(bytes),
                           static_cast<std::streamsize>(size));
    if (!*context->output) {
        context->error_message =
            "Failed to write output path: " + context->prepared_output->write_path().string();
        return false;
    }
    context->committed_bytes += static_cast<std::uint64_t>(size);
    std::string checkpoint_error;
    if (!context->prepared_output->save_resume_state(context->committed_bytes, checkpoint_error)) {
        context->error_message = checkpoint_error;
        return false;
    }
    return true;
}

StreamedDepotDownloadOutcome stream_http_download_to_prepared_output(
    cauth::core::platform::HttpRequest request,
    cauth::core::platform::PreparedFileWrite& prepared_output) {
    StreamedDepotDownloadOutcome outcome;

    for (int attempt = 0; attempt < 2; ++attempt) {
        std::ofstream output;
        std::string open_error;
        if (!prepared_output.open_binary_output(output, open_error)) {
            outcome.error_message = open_error;
            return outcome;
        }

        StreamedDepotWriteContext write_context;
        write_context.prepared_output = &prepared_output;
        write_context.output = &output;
        write_context.committed_bytes =
            prepared_output.resume_available() ? prepared_output.resume_offset() : 0;

        const auto use_range =
            attempt == 0 && prepared_output.resume_available() && prepared_output.resume_offset() > 0;
        request.use_range = use_range;
        request.range_start = use_range ? prepared_output.resume_offset() : 0;
        request.response_write_hook = &write_streamed_depot_bytes;
        request.response_write_user_data = &write_context;

        const auto response = cauth::core::platform::perform_platform_http_request(request);
        output.close();
        if (!output) {
            prepared_output.preserve_partial();
            outcome.error_message =
                "Failed to finalize output path: " + prepared_output.write_path().string();
            outcome.written_bytes = write_context.committed_bytes;
            return outcome;
        }

        if (!response.ok) {
            const auto action = current_download_interrupt_action();
            if (action == DownloadInterruptAction::Pause) {
                prepared_output.preserve_partial();
                outcome.paused = true;
                outcome.error_message = "operation paused";
                outcome.written_bytes = write_context.committed_bytes;
                return outcome;
            }
            if (action == DownloadInterruptAction::Cancel) {
                std::string discard_error;
                if (!prepared_output.discard_partial(discard_error) && outcome.error_message.empty()) {
                    outcome.error_message = discard_error;
                }
                outcome.canceled = true;
                outcome.error_message =
                    outcome.error_message.empty() ? "operation canceled" : outcome.error_message;
                outcome.written_bytes = 0;
                return outcome;
            }
            if (use_range && response.error_message == "HTTP range request was not honored") {
                std::string discard_error;
                if (!prepared_output.discard_partial(discard_error)) {
                    outcome.error_message = discard_error;
                    return outcome;
                }
                auto restarted = cauth::core::platform::prepare_file_write(
                    prepared_output.final_path(), prepared_output.options());
                if (!restarted.ok()) {
                    outcome.error_message = restarted.error_message();
                    return outcome;
                }
                prepared_output = std::move(restarted);
                continue;
            }

            if (write_context.committed_bytes > 0) {
                prepared_output.preserve_partial();
            }
            outcome.error_message = write_context.error_message.empty() ? response.error_message
                                                                        : write_context.error_message;
            outcome.written_bytes = write_context.committed_bytes;
            return outcome;
        }

        std::string commit_error;
        if (!prepared_output.commit(commit_error)) {
            outcome.error_message = commit_error;
            outcome.written_bytes = write_context.committed_bytes;
            return outcome;
        }

        outcome.ok = true;
        outcome.written_bytes = write_context.committed_bytes;
        return outcome;
    }

    outcome.error_message = "streamed download failed";
    return outcome;
}

DepotFileDownloadOutcome download_file_from_manifest_impl(
    const LoadedDepotManifest& loaded_manifest,
    std::size_t file_index,
    std::uint32_t max_count,
    const std::string& output_path,
    const cauth::core::platform::RouteSelection* route_selection,
    const cauth::core::platform::FileWriteOptions& write_options,
    DepotDownloadKind progress_kind,
    std::uint64_t completed_steps_base,
    std::uint64_t total_steps,
    std::uint64_t completed_bytes_base,
    std::uint64_t total_bytes,
    std::string_view phase_prefix,
    std::ostream& out,
    std::ostream& err) {
    if (!loaded_manifest.depot_key.has_value()) {
        err << "File download failed: depot key is required\n";
        return {1, false, 0};
    }

    const auto& file = loaded_manifest.manifest.files[file_index];
    const auto display_name = display_manifest_filename(file.filename);
    auto resumable_write_options = write_options;
    resumable_write_options.allow_resume = true;
    resumable_write_options.resume_token = make_file_resume_token(loaded_manifest, file);
    auto prepared_output = cauth::core::platform::prepare_file_write(
        std::filesystem::path{output_path}, resumable_write_options);
    if (!prepared_output.ok()) {
        err << prepared_output.error_message() << '\n';
        return {1, false, 0};
    }
    if (prepared_output.skipped()) {
        report_download_progress(DepotDownloadProgress{
            progress_kind,
            std::string{phase_prefix}.append(" skipped (existing output)"),
            display_name,
            completed_steps_base + 1,
            total_steps,
            completed_bytes_base,
            total_bytes,
        });
        out << "Skipping existing output for " << display_name << ": " << output_path << '\n';
        return {0, true, 0};
    }

    const auto cdn_servers = fetch_cdn_servers_for_download(max_count, route_selection, err);
    if (!cdn_servers.has_value()) {
        return {1, false, 0};
    }

    std::string write_error;
    std::ofstream output;
    if (!prepared_output.open_binary_output(output, write_error)) {
        err << write_error << '\n';
        return {1, false, 0};
    }

    cauth::core::depot::ManifestDownloader downloader;
    std::uint64_t completed_file_bytes =
        prepared_output.resume_available() ? prepared_output.resume_offset() : 0;
    const auto resume_chunk_index =
        resolve_resume_chunk_index(file, completed_file_bytes);
    if (!resume_chunk_index.has_value()) {
        output.close();
        std::string discard_error;
        if (!prepared_output.discard_partial(discard_error)) {
            err << discard_error << '\n';
            return {1, false, 0};
        }
        prepared_output = cauth::core::platform::prepare_file_write(
            std::filesystem::path{output_path}, resumable_write_options);
        if (!prepared_output.ok()) {
            err << prepared_output.error_message() << '\n';
            return {1, false, 0};
        }
        if (!prepared_output.open_binary_output(output, write_error)) {
            err << write_error << '\n';
            return {1, false, 0};
        }
        completed_file_bytes = 0;
    }
    report_download_progress(DepotDownloadProgress{
        progress_kind,
        std::string{phase_prefix}.append(" (preparing)"),
        display_name,
        completed_steps_base,
        total_steps,
        completed_bytes_base + completed_file_bytes,
        total_bytes,
    });
    for (std::size_t current_chunk_index =
             resume_chunk_index.value_or(0);
         current_chunk_index < file.chunks.size();
         ++current_chunk_index) {
        if (fail_if_download_interrupted(err)) {
            const auto action = current_download_interrupt_action();
            if (action == DownloadInterruptAction::Pause) {
                prepared_output.preserve_partial();
            } else if (action == DownloadInterruptAction::Cancel) {
                std::string discard_error;
                if (!prepared_output.discard_partial(discard_error) && !discard_error.empty()) {
                    err << discard_error << '\n';
                }
            }
            return {1, false, 0};
        }

        bool chunk_written = false;
        for (const auto& server : *cdn_servers) {
            if (fail_if_download_interrupted(err)) {
                const auto action = current_download_interrupt_action();
                if (action == DownloadInterruptAction::Pause) {
                    prepared_output.preserve_partial();
                } else if (action == DownloadInterruptAction::Cancel) {
                    std::string discard_error;
                    if (!prepared_output.discard_partial(discard_error) && !discard_error.empty()) {
                        err << discard_error << '\n';
                    }
                }
                return {1, false, 0};
            }

            DepotHttpProgressContext progress_context{
                progress_kind,
                std::string{phase_prefix}.append(" from ").append(server.vhost),
                display_name,
                completed_steps_base,
                total_steps,
                completed_bytes_base + completed_file_bytes,
                total_bytes,
                file.chunks[current_chunk_index].uncompressed_size,
            };
            report_download_progress(DepotDownloadProgress{
                progress_kind,
                progress_context.phase,
                display_name,
                completed_steps_base,
                total_steps,
                completed_bytes_base + completed_file_bytes,
                total_bytes,
            });
            out << "Downloading file chunk " << (current_chunk_index + 1) << '/'
                << file.chunks.size() << " from " << server.vhost << "...\n";
            const auto download_started = std::chrono::steady_clock::now();
            const auto response = downloader.download_raw_chunk(
                server,
                cauth::core::depot::ChunkDownloadRequest{
                    loaded_manifest.manifest.depot_id,
                    file.chunks[current_chunk_index].sha,
                },
                cauth::core::platform::HttpRequestCallbacks{
                    &report_depot_http_progress,
                    &is_depot_http_download_interrupted,
                    &progress_context,
                });
            record_cdn_attempt_result(
                server, response.ok, std::chrono::steady_clock::now() - download_started);
            if (!response.ok) {
                err << "Chunk download failed from " << response.url << ": "
                    << response.error_message << '\n';
                continue;
            }
            if (fail_if_download_interrupted(err)) {
                const auto action = current_download_interrupt_action();
                if (action == DownloadInterruptAction::Pause) {
                    prepared_output.preserve_partial();
                } else if (action == DownloadInterruptAction::Cancel) {
                    std::string discard_error;
                    if (!prepared_output.discard_partial(discard_error) && !discard_error.empty()) {
                        err << discard_error << '\n';
                    }
                }
                return {1, false, 0};
            }

            const auto processed = cauth::core::depot::process_depot_chunk(
                file.chunks[current_chunk_index], response.bytes, *loaded_manifest.depot_key);
            if (!processed.ok) {
                err << "Chunk process failed: " << processed.error_message << '\n';
                continue;
            }
            if (fail_if_download_interrupted(err)) {
                return {1, false, 0};
            }

            output.write(reinterpret_cast<const char*>(processed.bytes.data()),
                         static_cast<std::streamsize>(processed.bytes.size()));
            if (!output) {
                err << "Failed to write output path: " << prepared_output.write_path().string()
                    << '\n';
                return {1, false, 0};
            }

            completed_file_bytes += static_cast<std::uint64_t>(processed.bytes.size());
            if (!prepared_output.save_resume_state(completed_file_bytes, write_error)) {
                err << write_error << '\n';
                return {1, false, 0};
            }
            report_download_progress(DepotDownloadProgress{
                progress_kind,
                std::string{phase_prefix}.append(" chunk ").append(
                    std::to_string(current_chunk_index + 1)).append("/").append(
                    std::to_string(file.chunks.size())),
                display_name,
                completed_steps_base,
                total_steps,
                completed_bytes_base + completed_file_bytes,
                total_bytes,
            });
            chunk_written = true;
            break;
        }

        if (!chunk_written) {
            prepared_output.preserve_partial();
            err << "File download failed at chunk " << current_chunk_index << '\n';
            return {1, false, 0};
        }
    }

    output.close();
    if (!output) {
        err << "Failed to finalize output path: " << prepared_output.write_path().string() << '\n';
        return {1, false, 0};
    }
    if (!prepared_output.commit(write_error)) {
        err << write_error << '\n';
        return {1, false, 0};
    }

    report_download_progress(DepotDownloadProgress{
        progress_kind,
        std::string{phase_prefix}.append(" complete"),
        display_name,
        completed_steps_base + 1,
        total_steps,
        completed_bytes_base + completed_file_bytes,
        total_bytes,
    });
    return {0, false, completed_file_bytes};
}

} // namespace

void set_current_thread_depot_download_hooks(DepotDownloadProgressHook progress_hook,
                                             DepotDownloadCancelHook cancel_hook,
                                             DepotDownloadPauseHook pause_hook,
                                             void* user_data) {
    g_download_progress_hook = progress_hook;
    g_download_cancel_hook = cancel_hook;
    g_download_pause_hook = pause_hook;
    g_download_hook_user_data = user_data;
}

void clear_current_thread_depot_download_hooks() {
    g_download_progress_hook = nullptr;
    g_download_cancel_hook = nullptr;
    g_download_pause_hook = nullptr;
    g_download_hook_user_data = nullptr;
}

DepotDownloadRouteReport probe_download_routes(
    std::uint32_t max_count,
    const cauth::core::platform::RouteSelection* route_selection) {
    DepotDownloadRouteReport report;
    std::ostringstream err;
    const auto cdn_servers = fetch_cdn_servers_for_download(max_count, route_selection, err);
    if (!cdn_servers.has_value()) {
        report.ok = false;
        report.module_status = "failed";
        report.message = trim_trailing_line_breaks(err.str());
        return report;
    }

    auto& cache = cauth::core::platform::EndpointRouteCache::instance();
    report.ok = true;
    report.module_status = "succeeded";
    report.message = "ok";
    report.routes.reserve(cdn_servers->size());
    for (const auto& server : *cdn_servers) {
        const auto snapshot = cache.snapshot("steam.depot.cdn", cdn_server_route_key(server));
        report.routes.push_back(DepotDownloadRouteEntry{
            server.vhost.empty() ? server.host : server.vhost,
            server.protocol == cauth::core::depot::CdnServerProtocol::Https ? "https" : "http",
            server.type,
            server.use_as_proxy,
            snapshot.has_fresh_latency,
            snapshot.latency_ms,
            snapshot.has_recent_success,
            snapshot.has_recent_failure,
            snapshot.success_count,
            snapshot.failure_count,
        });
    }
    return report;
}

int print_download_routes(std::uint32_t max_count,
                          const cauth::core::platform::RouteSelection* route_selection,
                          std::ostream& out,
                          std::ostream& err) {
    const auto report = probe_download_routes(max_count, route_selection);
    if (!report.ok) {
        err << report.message << '\n';
        return 1;
    }

    out << "Depot CDN routes: " << report.routes.size() << '\n';
    for (std::size_t index = 0; index < report.routes.size(); ++index) {
        const auto& route = report.routes[index];
        out << "  [" << (index + 1) << "] " << route.protocol << " " << route.endpoint
            << " latency="
            << (route.latency_known ? std::to_string(route.latency_ms) + "ms" : "unknown")
            << " recent_success=" << (route.recent_success ? "true" : "false")
            << " recent_failure=" << (route.recent_failure ? "true" : "false")
            << " success_count=" << route.success_count
            << " failure_count=" << route.failure_count;
        if (!route.server_type.empty()) {
            out << " type=" << route.server_type;
        }
        if (route.use_as_proxy) {
            out << " proxy=true";
        }
        out << '\n';
    }
    return 0;
}

std::optional<cauth::core::depot::AppInfo> fetch_app_info_from_cm(std::uint64_t steam_id,
                                                                  std::uint32_t app_id,
                                                                  std::uint32_t max_count,
                                                                  const cauth::core::platform::RouteSelection* route_selection,
                                                                  std::ostream& out,
                                                                  std::ostream& err) {
    const auto store = cauth::core::platform::make_platform_session_repository();
    cauth::steam::auth::StoredSteamAuthProvider auth_provider{*store};
    cauth::core::depot::DepotCmClient depot_client{
        auth_provider,
        std::to_string(steam_id),
        route_selection,
        &out,
        &err};
    return depot_client.fetch_app_info(app_id, max_count);
}

std::optional<cauth::core::depot::DepotDecryptionKeyResponse> fetch_depot_key_from_cm(
    std::uint64_t steam_id,
    std::uint32_t app_id,
    std::uint32_t depot_id,
    std::uint32_t max_count,
    const cauth::core::platform::RouteSelection* route_selection,
    std::ostream& out,
    std::ostream& err) {
    const auto store = cauth::core::platform::make_platform_session_repository();
    cauth::steam::auth::StoredSteamAuthProvider auth_provider{*store};
    cauth::core::depot::DepotCmClient depot_client{
        auth_provider,
        std::to_string(steam_id),
        route_selection,
        &out,
        &err};
    return depot_client.fetch_depot_key(app_id, depot_id, max_count);
}

std::optional<cauth::core::depot::ManifestRequestCodeResponse>
fetch_manifest_request_code_from_cm(
    std::uint64_t steam_id,
    const cauth::core::depot::ManifestRequestCodeRequest& request,
    std::uint32_t max_count,
    const cauth::core::platform::RouteSelection* route_selection,
    std::ostream& out,
    std::ostream& err) {
    const auto store = cauth::core::platform::make_platform_session_repository();
    cauth::steam::auth::StoredSteamAuthProvider auth_provider{*store};
    cauth::core::depot::DepotCmClient depot_client{
        auth_provider,
        std::to_string(steam_id),
        route_selection,
        &out,
        &err};
    return depot_client.fetch_manifest_request_code(request, max_count);
}

int print_branches(std::uint64_t steam_id,
                   std::uint32_t app_id,
                   std::uint32_t max_count,
                   const cauth::core::platform::RouteSelection* route_selection,
                   std::ostream& out,
                   std::ostream& err) {
    const auto app_info = fetch_app_info_from_cm(
        steam_id, app_id, max_count, route_selection, out, err);
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
                    const cauth::core::platform::RouteSelection* route_selection,
                    std::ostream& out,
                    std::ostream& err) {
    const auto app_info = fetch_app_info_from_cm(
        steam_id, app_id, max_count, route_selection, out, err);
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
                    const cauth::core::platform::RouteSelection* route_selection,
                    std::ostream& out,
                    std::ostream& err) {
    const auto app_info = fetch_app_info_from_cm(
        steam_id, app_id, max_count, route_selection, out, err);
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
                    const cauth::core::platform::RouteSelection* route_selection,
                    std::ostream& out,
                    std::ostream& err) {
    const auto response = fetch_depot_key_from_cm(
        steam_id, app_id, depot_id, max_count, route_selection, out, err);
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
                                const cauth::core::platform::RouteSelection* route_selection,
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
        route_selection,
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
        out << "  file[" << index << "]=" << display_manifest_filename(file.filename)
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
        const auto display_name = display_manifest_filename(file.filename);
        if (!contains_ascii_case_insensitive(display_name, filter_text)) continue;
        ++matched;
        if (printed >= list_limit) continue;
        out << "file[" << index << "]=" << display_name
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
            report->module_status = "failed";
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
        report->module_status = "verifying";
        report->total_count = count_non_directory_manifest_files(manifest);
        report->entries.reserve(static_cast<std::size_t>(report->total_count));
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
            report->module_status = "failed";
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
            report->module_status = "failed";
        }
        return 1;
    }
    if (manifest.filenames_encrypted && !loaded_manifest.depot_key.has_value()) {
        err << "Depot key is required for local verify because manifest filenames are encrypted\n";
        if (report != nullptr) {
            report->fatal_error = true;
            report->module_status = "failed";
        }
        return 1;
    }

    const auto total_files = count_non_directory_manifest_files(manifest);
    report_download_progress(DepotDownloadProgress{
        DepotDownloadKind::VerifyLocal,
        "Verifying local files",
        local_root,
        0,
        total_files,
        0,
        manifest.total_uncompressed_size,
    });

    std::uint64_t completed_bytes = 0;

    for (const auto& file : manifest.files) {
        if (cauth::core::depot::depot_file_is_directory(file)) {
            continue;
        }
        if (fail_if_download_interrupted(err)) {
            if (report != nullptr) {
                report->fatal_error = true;
                report->module_status = "failed";
            }
            return 1;
        }
        const auto display_name = display_manifest_filename(file.filename);
        if (!contains_ascii_case_insensitive(display_name, filter_text)) {
            if (report != nullptr) {
                auto entry = make_local_verify_entry_base(file, display_name);
                entry.status = LocalVerifyStatus::FilteredOut;
                entry.reason = "filtered out by filter text";
                report->entries.push_back(std::move(entry));
            }
            ++filtered_out;
            completed_bytes += file.size;
            report_download_progress(DepotDownloadProgress{
                DepotDownloadKind::VerifyLocal,
                "Verifying local files",
                display_name,
                checked + filtered_out,
                total_files,
                completed_bytes,
                manifest.total_uncompressed_size,
            });
            continue;
        }
        ++checked;

        std::ostringstream path_err;
        const auto output_path = make_safe_manifest_output_path(local_root, file.filename, path_err);
        if (!output_path.has_value()) {
            const auto reason = trim_trailing_line_breaks(path_err.str());
            err << path_err.str();
            if (report != nullptr) {
                auto entry = make_local_verify_entry_base(file, display_name);
                entry.status = LocalVerifyStatus::Mismatched;
                entry.reason = reason.empty() ? "unsafe manifest output path" : reason;
                report->entries.push_back(std::move(entry));
            }
            ++mismatched;
            completed_bytes += file.size;
            report_download_progress(DepotDownloadProgress{
                DepotDownloadKind::VerifyLocal,
                "Verifying local files",
                display_name,
                checked + filtered_out,
                total_files,
                completed_bytes,
                manifest.total_uncompressed_size,
            });
            continue;
        }

        auto entry = make_local_verify_entry_base(file, display_name);
        entry.local_path = output_path->string();
        entry.actual_size = read_file_size_or_zero(*output_path);
        const auto verify_result =
            cauth::core::depot::verify_depot_file_on_disk(*output_path, file);
        if (verify_result.ok) {
            ++matched;
            if (!cauth::core::depot::depot_file_has_binary_verification(file)) {
                entry.status = LocalVerifyStatus::SizeOnly;
                entry.reason = "size-only verification";
                ++size_only;
                out << "SIZE_ONLY file=" << display_name
                    << " path=" << output_path->string() << '\n';
            } else {
                entry.status = LocalVerifyStatus::Ok;
                entry.reason = "binary verification passed";
                if (!entry.expected_sha_hex.empty()) {
                    entry.actual_sha_hex = entry.expected_sha_hex;
                }
                out << "OK file=" << display_name
                    << " path=" << output_path->string() << '\n';
            }
            if (report != nullptr) {
                report->entries.push_back(std::move(entry));
            }
            completed_bytes += file.size;
            report_download_progress(DepotDownloadProgress{
                DepotDownloadKind::VerifyLocal,
                "Verifying local files",
                display_name,
                checked + filtered_out,
                total_files,
                completed_bytes,
                manifest.total_uncompressed_size,
            });
            continue;
        }

        if (verify_result.error_message.find("missing") != std::string::npos) {
            entry.status = LocalVerifyStatus::MissingLocal;
            entry.actual_size = 0;
            entry.reason = verify_result.error_message;
            if (report != nullptr) {
                report->entries.push_back(std::move(entry));
            }
            ++missing;
            out << "MISSING file=" << display_name
                << " path=" << output_path->string() << '\n';
            completed_bytes += file.size;
            report_download_progress(DepotDownloadProgress{
                DepotDownloadKind::VerifyLocal,
                "Verifying local files",
                display_name,
                checked + filtered_out,
                total_files,
                completed_bytes,
                manifest.total_uncompressed_size,
            });
            continue;
        }

        entry.status = LocalVerifyStatus::Mismatched;
        entry.reason = verify_result.error_message;
        if (report != nullptr) {
            report->entries.push_back(std::move(entry));
        }
        ++mismatched;
        out << "MISMATCH file=" << display_name
            << " path=" << output_path->string()
            << " reason=" << verify_result.error_message << '\n';
        completed_bytes += file.size;
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::VerifyLocal,
            "Verifying local files",
            display_name,
            checked + filtered_out,
            total_files,
            completed_bytes,
            manifest.total_uncompressed_size,
        });
    }

    if (report != nullptr) {
        report->checked_count = checked;
        report->ok_count = matched;
        report->missing_count = missing;
        report->mismatched_count = mismatched;
        report->size_only_count = size_only;
        report->filtered_out_count = filtered_out;
        report->module_status =
            (missing == 0 && mismatched == 0) ? "succeeded" : "failed";
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
                              const cauth::core::platform::RouteSelection* route_selection,
                              const cauth::core::platform::FileWriteOptions& write_options,
                              std::ostream& out,
                              std::ostream& err) {
    auto resumable_write_options = write_options;
    resumable_write_options.allow_resume = true;
    resumable_write_options.resume_token =
        make_manifest_resume_token(depot_id, manifest_gid, request_code);
    auto prepared_output = cauth::core::platform::prepare_file_write(
        std::filesystem::path{output_path}, resumable_write_options);
    if (!prepared_output.ok()) {
        err << prepared_output.error_message() << '\n';
        return 1;
    }
    if (prepared_output.skipped()) {
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::Manifest,
            "Manifest download skipped (existing output)",
            output_path,
            1,
            1,
            0,
            0,
        });
        out << "Skipping existing output: " << output_path << '\n';
        return 0;
    }

    const auto cdn_servers = fetch_cdn_servers_for_download(max_count, route_selection, err);
    if (!cdn_servers.has_value()) {
        return 1;
    }

    const cauth::core::depot::ManifestDownloadRequest download_request{
        depot_id,
        manifest_gid,
        request_code,
    };
    const auto total_servers = static_cast<std::uint64_t>(cdn_servers->size());
    for (std::size_t server_index = 0; server_index < cdn_servers->size(); ++server_index) {
        if (fail_if_download_interrupted(err)) {
            return 1;
        }
        const auto& server = (*cdn_servers)[server_index];
        DepotHttpProgressContext progress_context{
            DepotDownloadKind::Manifest,
            "Requesting manifest from " + server.vhost,
            output_path,
            static_cast<std::uint64_t>(server_index),
            total_servers,
            0,
            0,
            0,
        };
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::Manifest,
            progress_context.phase,
            output_path,
            static_cast<std::uint64_t>(server_index),
            total_servers,
            0,
            0,
        });
        out << "Downloading manifest from " << server.vhost << "...\n";
        const auto download_started = std::chrono::steady_clock::now();
        cauth::core::platform::HttpRequest request;
        request.url = cauth::core::depot::build_manifest_url(server, download_request);
        request.connect_timeout_ms = 15000;
        request.read_timeout_ms = 0;
        request.callbacks = cauth::core::platform::HttpRequestCallbacks{
            &report_depot_http_progress,
            &is_depot_http_download_interrupted,
            &progress_context,
        };
        const auto result = stream_http_download_to_prepared_output(request, prepared_output);
        record_cdn_attempt_result(
            server, result.ok, std::chrono::steady_clock::now() - download_started);
        if (result.paused) {
            err << "operation paused\n";
            return 1;
        }
        if (result.canceled) {
            err << "operation canceled\n";
            return 1;
        }
        if (!result.ok) {
            err << "Manifest download failed from " << request.url << ": " << result.error_message << '\n';
            continue;
        }
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::Manifest,
            "Manifest downloaded",
            output_path,
            total_servers,
            total_servers,
            result.written_bytes,
            result.written_bytes,
        });
        out << "Manifest downloaded: " << result.written_bytes << " bytes -> " << output_path
            << '\n';
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
                                 const cauth::core::platform::RouteSelection* route_selection,
                                 const cauth::core::platform::FileWriteOptions& write_options,
                                 std::ostream& out,
                                 std::ostream& err) {
    const auto& file = loaded_manifest.manifest.files[file_index];
    const auto display_name = display_manifest_filename(file.filename);
    const auto cdn_servers = fetch_cdn_servers_for_download(max_count, route_selection, err);
    if (!cdn_servers.has_value()) return 1;

    const auto& selected_chunk = file.chunks[chunk_index];
    const auto chunk_sha_hex = cauth::core::cm::bytes_to_hex(selected_chunk.sha);
    const auto total_servers = static_cast<std::uint64_t>(cdn_servers->size());
    report_download_progress(DepotDownloadProgress{
        DepotDownloadKind::Chunk,
        "Preparing chunk download",
        display_name,
        0,
        1,
        0,
        process_chunk ? selected_chunk.uncompressed_size : selected_chunk.compressed_size,
    });

    auto effective_write_options = write_options;
    effective_write_options.allow_resume = !process_chunk;
    effective_write_options.resume_token = make_chunk_resume_token(
        loaded_manifest.manifest.depot_id, chunk_sha_hex, false);
    auto prepared_output = cauth::core::platform::prepare_file_write(
        std::filesystem::path{output_path}, effective_write_options);
    if (!prepared_output.ok()) {
        err << prepared_output.error_message() << '\n';
        return 1;
    }
    if (prepared_output.skipped()) {
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::Chunk,
            "Chunk download skipped (existing output)",
            display_name,
            1,
            1,
            0,
            process_chunk ? selected_chunk.uncompressed_size
                          : selected_chunk.compressed_size,
        });
        out << "Skipping existing output for chunk " << (chunk_index + 1) << "/"
            << file.chunks.size() << ": " << output_path << '\n';
        return 0;
    }

    const auto raw_chunk_path =
        std::filesystem::path{output_path}.string() + ".cauthdownload.raw";
    cauth::core::platform::FileWriteOptions raw_write_options;
    raw_write_options.mode = cauth::core::platform::FileWriteMode::Overwrite;
    raw_write_options.atomic_write = false;
    raw_write_options.allow_resume = true;
    raw_write_options.resume_token = make_chunk_resume_token(
        loaded_manifest.manifest.depot_id, chunk_sha_hex, process_chunk);
    auto prepared_raw_chunk = cauth::core::platform::prepare_file_write(
        std::filesystem::path{raw_chunk_path}, raw_write_options);
    if (process_chunk && !prepared_raw_chunk.ok()) {
        err << prepared_raw_chunk.error_message() << '\n';
        return 1;
    }

    for (std::size_t server_index = 0; server_index < cdn_servers->size(); ++server_index) {
        if (fail_if_download_interrupted(err)) {
            return 1;
        }
        const auto& server = (*cdn_servers)[server_index];
        DepotHttpProgressContext progress_context{
            DepotDownloadKind::Chunk,
            "Downloading chunk from " + server.vhost,
            display_name,
            static_cast<std::uint64_t>(server_index),
            total_servers,
            0,
            process_chunk ? selected_chunk.uncompressed_size : selected_chunk.compressed_size,
            process_chunk ? selected_chunk.uncompressed_size : 0,
        };
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::Chunk,
            progress_context.phase,
            display_name,
            static_cast<std::uint64_t>(server_index),
            total_servers,
            0,
            process_chunk ? selected_chunk.uncompressed_size : selected_chunk.compressed_size,
        });
        out << "Downloading file chunk " << (chunk_index + 1) << '/' << file.chunks.size()
            << " from " << server.vhost << "...\n";
        const auto download_started = std::chrono::steady_clock::now();
        cauth::core::platform::HttpRequest request;
        request.url = cauth::core::depot::build_chunk_url(
            server,
            cauth::core::depot::ChunkDownloadRequest{
                loaded_manifest.manifest.depot_id,
                selected_chunk.sha,
            });
        request.connect_timeout_ms = 15000;
        request.read_timeout_ms = 0;
        request.callbacks = cauth::core::platform::HttpRequestCallbacks{
            &report_depot_http_progress,
            &is_depot_http_download_interrupted,
            &progress_context,
        };
        auto& target_output = process_chunk ? prepared_raw_chunk : prepared_output;
        const auto response = stream_http_download_to_prepared_output(request, target_output);
        record_cdn_attempt_result(
            server, response.ok, std::chrono::steady_clock::now() - download_started);
        if (response.paused) {
            err << "operation paused\n";
            return 1;
        }
        if (response.canceled) {
            err << "operation canceled\n";
            return 1;
        }
        if (!response.ok) {
            err << "Chunk download failed from " << request.url << ": "
                << response.error_message << '\n';
            continue;
        }

        std::uint64_t output_size = response.written_bytes;
        if (process_chunk) {
            if (!loaded_manifest.depot_key.has_value()) {
                err << "Chunk process failed: depot key is required\n";
                return 1;
            }
            const auto raw_bytes = read_file_bytes(prepared_raw_chunk.final_path());
            if (!raw_bytes.has_value()) {
                err << "Chunk process failed: failed to read " << prepared_raw_chunk.final_path().string()
                    << '\n';
                return 1;
            }
            const auto processed = cauth::core::depot::process_depot_chunk(
                selected_chunk,
                *raw_bytes,
                *loaded_manifest.depot_key);
            if (!processed.ok) {
                err << "Chunk process failed: " << processed.error_message << '\n';
                return 1;
            }
            std::string write_error;
            if (!prepared_output.write_all(processed.bytes, write_error)) {
                err << write_error << '\n';
                return 1;
            }
            std::string discard_error;
            if (!prepared_raw_chunk.discard_partial(discard_error) && !discard_error.empty()) {
                err << discard_error << '\n';
                return 1;
            }
            output_size = processed.bytes.size();
        }
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::Chunk,
            "Chunk downloaded",
            display_name,
            1,
            1,
            output_size,
            process_chunk ? selected_chunk.uncompressed_size : selected_chunk.compressed_size,
        });
        out << "Chunk downloaded: " << output_size << " bytes -> " << output_path << '\n';
        return 0;
    }

    err << "Chunk download failed for all CDN servers\n";
    return 1;
}

int download_file_from_manifest(const LoadedDepotManifest& loaded_manifest,
                                std::size_t file_index,
                                std::uint32_t max_count,
                                const std::string& output_path,
                                const cauth::core::platform::RouteSelection* route_selection,
                                const cauth::core::platform::FileWriteOptions& write_options,
                                std::ostream& out,
                                std::ostream& err) {
    const auto& file = loaded_manifest.manifest.files[file_index];
    const auto display_name = display_manifest_filename(file.filename);
    const auto outcome = download_file_from_manifest_impl(
        loaded_manifest,
        file_index,
        max_count,
        output_path,
        route_selection,
        write_options,
        DepotDownloadKind::File,
        0,
        1,
        0,
        file.size,
        "Downloading file",
        out,
        err);
    if (outcome.exit_code != 0) {
        return outcome.exit_code;
    }
    if (outcome.skipped) {
        out << "File skipped: " << display_name << " -> " << output_path << '\n';
        return 0;
    }
    out << "File downloaded: " << display_name << " -> " << output_path << '\n';
    return 0;
}

int download_all_files_from_manifest(const LoadedDepotManifest& loaded_manifest,
                                     std::uint32_t max_count,
                                     const std::string& output_root,
                                     const cauth::core::platform::RouteSelection* route_selection,
                                     const cauth::core::platform::FileWriteOptions& write_options,
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
    std::uint64_t skipped_files = 0;
    const auto batch_resume_state_path = make_all_files_resume_state_path(output_root);
    const auto batch_resume_token = make_all_files_resume_token(loaded_manifest, output_root);
    std::size_t resume_file_index = 0;

    {
        std::error_code state_ec;
        if (std::filesystem::exists(batch_resume_state_path, state_ec) && !state_ec) {
            DepotBatchResumeState batch_state;
            std::string state_error;
            if (load_all_files_resume_state(batch_resume_state_path, batch_state, state_error) &&
                batch_state.token == batch_resume_token) {
                resume_file_index = batch_state.next_file_index;
                completed_bytes = batch_state.completed_bytes;
                out << "Resuming all-files download from file index " << resume_file_index
                    << "...\n";
            } else {
                clear_all_files_resume_state(batch_resume_state_path, state_error);
            }
        }
    }

    report_download_progress(DepotDownloadProgress{
        DepotDownloadKind::AllFiles,
        "Preparing file batch download",
        output_root,
        0,
        total_files,
        completed_bytes,
        total_bytes,
    });

    std::uint64_t completed_files = 0;
    for (std::size_t file_index = 0; file_index < files.size(); ++file_index) {
        if (fail_if_download_interrupted(err)) {
            return 1;
        }
        const auto& file = files[file_index];
        const auto display_name = display_manifest_filename(file.filename);
        if (cauth::core::depot::depot_file_is_directory(file)) {
            if (!ensure_manifest_directory_exists(output_root, file.filename, err)) {
                return 1;
            }
            if (file_index >= resume_file_index) {
                out << "Created manifest directory: " << display_name << '\n';
            }
            continue;
        }
        if (file_index < resume_file_index) {
            ++completed_files;
            continue;
        }
        const auto output_path = make_safe_manifest_output_path(output_root, file.filename, err);
        if (!output_path.has_value()) {
            return 1;
        }

        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::AllFiles,
            "Downloading file " + std::to_string(file_index + 1) + "/" +
                std::to_string(files.size()),
            display_name,
            completed_files,
            total_files,
            completed_bytes,
            total_bytes,
        });
        out << "Downloading manifest file " << (file_index + 1) << '/' << files.size()
            << ": " << display_name << " -> " << output_path->string() << '\n';

        const auto outcome = download_file_from_manifest_impl(
            loaded_manifest,
            file_index,
            max_count,
            output_path->string(),
            route_selection,
            write_options,
            DepotDownloadKind::AllFiles,
            completed_files,
            total_files,
            completed_bytes,
            total_bytes,
            "Downloading file " + std::to_string(file_index + 1) + "/" +
                std::to_string(files.size()),
            out,
            err);
        if (outcome.exit_code != 0) {
            const auto action = current_download_interrupt_action();
            if (action == DownloadInterruptAction::Pause) {
                std::string state_error;
                const DepotBatchResumeState batch_state{
                    batch_resume_token,
                    file_index,
                    completed_bytes,
                };
                if (!save_all_files_resume_state(batch_resume_state_path, batch_state, state_error)) {
                    err << state_error << '\n';
                }
            } else if (action == DownloadInterruptAction::Cancel) {
                std::string state_error;
                if (!clear_all_files_resume_state(batch_resume_state_path, state_error) &&
                    !state_error.empty()) {
                    err << state_error << '\n';
                }
            }
            err << "All-files download failed at file " << display_name << '\n';
            return outcome.exit_code;
        }
        if (outcome.skipped) {
            ++skipped_files;
        }

        completed_bytes += outcome.written_bytes;
        ++completed_files;
        {
            std::string state_error;
            const DepotBatchResumeState batch_state{
                batch_resume_token,
                file_index + 1,
                completed_bytes,
            };
            if (!save_all_files_resume_state(batch_resume_state_path, batch_state, state_error)) {
                err << state_error << '\n';
                return 1;
            }
        }
        report_download_progress(DepotDownloadProgress{
            DepotDownloadKind::AllFiles,
            (outcome.skipped ? "Skipped existing file " : "Downloaded file ") +
                std::to_string(file_index + 1) + "/" + std::to_string(files.size()),
            display_name,
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
    if (skipped_files != 0) {
        out << "Skipped existing files: " << skipped_files << '\n';
    }
    std::string state_error;
    if (!clear_all_files_resume_state(batch_resume_state_path, state_error) &&
        !state_error.empty()) {
        err << state_error << '\n';
        return 1;
    }
    return 0;
}

} // namespace cauth::steam::depot
