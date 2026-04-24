#include "cli/steam_cli.hpp"
#include "cli/cli_support.hpp"

#include "core/platform/route_selection.hpp"
#include "steam/depot/steam_depot_application.hpp"

#include <atomic>
#include <csignal>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace cauth::cli::support;

std::atomic_bool g_depot_cli_cancel_requested{false};
std::atomic_bool g_depot_cli_pause_requested{false};
std::atomic_bool g_depot_cli_pause_on_sigint{false};

void on_depot_cli_signal(int) {
    if (g_depot_cli_pause_on_sigint.load()) {
        g_depot_cli_pause_requested.store(true);
    } else {
        g_depot_cli_cancel_requested.store(true);
    }
}

bool depot_cli_cancel_requested(void*) {
    return g_depot_cli_cancel_requested.load();
}

bool depot_cli_pause_requested(void*) {
    return g_depot_cli_pause_requested.load();
}

int apply_write_mode_flag(cauth::core::platform::FileWriteOptions& write_options,
                          bool& has_write_mode,
                          std::string_view arg,
                          std::ostream& err) {
    using WriteMode = cauth::core::platform::FileWriteMode;
    WriteMode parsed_mode = WriteMode::Overwrite;
    if (arg == "--overwrite") {
        parsed_mode = WriteMode::Overwrite;
    } else if (arg == "--skip-existing") {
        parsed_mode = WriteMode::SkipExisting;
    } else if (arg == "--fail-if-exists") {
        parsed_mode = WriteMode::FailIfExists;
    } else {
        return 0;
    }
    if (has_write_mode && write_options.mode != parsed_mode) {
        err << "only one of --overwrite, --skip-existing, or --fail-if-exists may be used\n";
        return -1;
    }
    write_options.mode = parsed_mode;
    has_write_mode = true;
    return 1;
}

struct DepotCliProgressState {
    ProgressLineState line_state;
};

void on_depot_progress(const cauth::steam::depot::DepotDownloadProgress& progress, void* user_data) {
    auto* state = static_cast<DepotCliProgressState*>(user_data);
    if (state == nullptr) {
        return;
    }

    std::string line = "steam depot";
    if (!progress.module_status.empty()) {
        line += " [" + progress.module_status + "]";
    }
    line += ": " + progress.phase;
    if (progress.total_steps != 0) {
        line += " [";
        line += std::to_string(progress.completed_steps);
        line += "/";
        line += std::to_string(progress.total_steps);
        line += "]";
    }
    if (progress.total_bytes != 0 || progress.completed_bytes != 0) {
        line += " ";
        line += format_byte_count(progress.completed_bytes);
        if (progress.total_bytes != 0) {
            line += "/";
            line += format_byte_count(progress.total_bytes);
        }
    }
    if (!progress.target.empty()) {
        line += " ";
        line += truncate_progress_text(progress.target);
    }
    print_progress_line(std::cerr, state->line_state, line);
}

struct ScopedDepotCliProgress {
    explicit ScopedDepotCliProgress(bool enabled, bool pause_on_sigint)
        : enabled_(enabled), pause_on_sigint_(pause_on_sigint) {
        g_depot_cli_cancel_requested.store(false);
        g_depot_cli_pause_requested.store(false);
        g_depot_cli_pause_on_sigint.store(pause_on_sigint_);
        previous_handler_ = std::signal(SIGINT, &on_depot_cli_signal);
        cauth::steam::depot::set_current_thread_depot_download_hooks(
            enabled_ ? &on_depot_progress : nullptr,
            &depot_cli_cancel_requested,
            &depot_cli_pause_requested,
            &state_);
    }

    ~ScopedDepotCliProgress() {
        cauth::steam::depot::clear_current_thread_depot_download_hooks();
        if (enabled_) {
            finish_progress_line(std::cerr, state_.line_state);
        }
        g_depot_cli_pause_on_sigint.store(false);
        std::signal(SIGINT, previous_handler_);
    }

  private:
    bool enabled_ = false;
    bool pause_on_sigint_ = false;
    DepotCliProgressState state_;
    void (*previous_handler_)(int) = SIG_DFL;
};

enum class DepotCommandKind {
    Routes,
    Branches,
    Manifests,
    Key,
    Preflight,
    ManifestCode,
    ManifestDownload,
    ManifestInfo,
    FileList,
    VerifyLocal,
    ChunkDownload,
    FileDownload,
    AllFilesDownload,
};

struct ParsedDepotCommand {
    DepotCommandKind kind = DepotCommandKind::Branches;
    std::uint64_t steam_id = 0;
    std::uint32_t app_id = 0;
    std::uint32_t depot_id = 0;
    std::uint32_t max_count = 5;
    std::uint64_t manifest_gid = 0;
    std::uint64_t request_code = 0;
    std::size_t file_index = 0;
    std::size_t chunk_index = 0;
    std::size_t list_limit = 50;
    bool has_file_index = false;
    bool has_chunk_index = false;
    bool process_chunk = false;
    std::string branch = "public";
    std::string depot_key_hex;
    std::string file_path;
    std::string filter_text;
    std::string input_path;
    std::string output_path;
    std::string local_root;
    bool pause_on_sigint = false;
    cauth::core::platform::RouteSelection route_selection;
    cauth::core::platform::FileWriteOptions write_options;
    bool has_write_mode = false;
};

struct ParsedDepotResult {
    bool ok = false;
    int exit_code = 2;
    ParsedDepotCommand command;
};

ParsedDepotResult parse_depot_command(int argc, char** argv) {
    ParsedDepotResult result;
    if (argc < 3) {
        std::cerr << "missing steam depot command\n";
        cauth::cli::print_cli_usage();
        return result;
    }

    const std::string_view subcommand = argv[2];
    if (subcommand == "routes") {
        result.command.kind = DepotCommandKind::Routes;
    } else if (subcommand == "branches") {
        result.command.kind = DepotCommandKind::Branches;
    } else if (subcommand == "manifests") {
        result.command.kind = DepotCommandKind::Manifests;
    } else if (subcommand == "key") {
        result.command.kind = DepotCommandKind::Key;
    } else if (subcommand == "preflight") {
        result.command.kind = DepotCommandKind::Preflight;
    } else if (subcommand == "manifest-code") {
        result.command.kind = DepotCommandKind::ManifestCode;
    } else if (subcommand == "manifest-download") {
        result.command.kind = DepotCommandKind::ManifestDownload;
    } else if (subcommand == "manifest-info") {
        result.command.kind = DepotCommandKind::ManifestInfo;
    } else if (subcommand == "file-list") {
        result.command.kind = DepotCommandKind::FileList;
    } else if (subcommand == "verify-local") {
        result.command.kind = DepotCommandKind::VerifyLocal;
    } else if (subcommand == "chunk-download") {
        result.command.kind = DepotCommandKind::ChunkDownload;
    } else if (subcommand == "file-download") {
        result.command.kind = DepotCommandKind::FileDownload;
    } else if (subcommand == "all-files-download") {
        result.command.kind = DepotCommandKind::AllFilesDownload;
    } else {
        std::cerr << "unknown steam depot command: " << subcommand << '\n';
        cauth::cli::print_cli_usage();
        return result;
    }

    for (int index = 3; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--app-id" && index + 1 < argc) {
            result.command.app_id = static_cast<std::uint32_t>(std::max(0, std::atoi(argv[++index])));
            continue;
        }
        if (arg == "--steam-id" && index + 1 < argc) {
            result.command.steam_id = parse_u64_arg(argv[++index]);
            continue;
        }
        if (arg == "--max-count" && index + 1 < argc) {
            result.command.max_count = static_cast<std::uint32_t>(std::max(1, std::atoi(argv[++index])));
            continue;
        }
        if ((result.command.kind == DepotCommandKind::ManifestDownload ||
             result.command.kind == DepotCommandKind::ChunkDownload ||
             result.command.kind == DepotCommandKind::FileDownload ||
             result.command.kind == DepotCommandKind::AllFilesDownload) &&
            arg == "--signal-action" && index + 1 < argc) {
            const std::string_view action = argv[++index];
            if (action == "pause") {
                result.command.pause_on_sigint = true;
            } else if (action == "cancel") {
                result.command.pause_on_sigint = false;
            } else {
                std::cerr << "unknown signal action: " << action << '\n';
                return result;
            }
            continue;
        }
        if ((result.command.kind == DepotCommandKind::Key ||
             result.command.kind == DepotCommandKind::ManifestCode ||
             result.command.kind == DepotCommandKind::ManifestDownload) &&
            arg == "--depot-id" && index + 1 < argc) {
            result.command.depot_id = static_cast<std::uint32_t>(std::max(0, std::atoi(argv[++index])));
            continue;
        }
        if ((result.command.kind == DepotCommandKind::ManifestCode ||
             result.command.kind == DepotCommandKind::ManifestDownload) &&
            arg == "--manifest-gid" && index + 1 < argc) {
            result.command.manifest_gid = parse_u64_arg(argv[++index]);
            continue;
        }
        if (result.command.kind == DepotCommandKind::ManifestDownload &&
            arg == "--request-code" && index + 1 < argc) {
            result.command.request_code = parse_u64_arg(argv[++index]);
            continue;
        }
        if ((result.command.kind == DepotCommandKind::ManifestDownload ||
             result.command.kind == DepotCommandKind::ChunkDownload ||
             result.command.kind == DepotCommandKind::FileDownload) &&
            arg == "--out" && index + 1 < argc) {
            result.command.output_path = argv[++index];
            continue;
        }
        if (result.command.kind == DepotCommandKind::AllFilesDownload &&
            arg == "--out-dir" && index + 1 < argc) {
            result.command.output_path = argv[++index];
            continue;
        }
        if ((result.command.kind == DepotCommandKind::ManifestInfo ||
             result.command.kind == DepotCommandKind::FileList ||
             result.command.kind == DepotCommandKind::VerifyLocal ||
             result.command.kind == DepotCommandKind::ChunkDownload ||
             result.command.kind == DepotCommandKind::FileDownload ||
             result.command.kind == DepotCommandKind::AllFilesDownload) &&
            arg == "--in" && index + 1 < argc) {
            result.command.input_path = argv[++index];
            continue;
        }
        if ((result.command.kind == DepotCommandKind::ManifestInfo ||
             result.command.kind == DepotCommandKind::FileList ||
             result.command.kind == DepotCommandKind::VerifyLocal ||
             result.command.kind == DepotCommandKind::ChunkDownload ||
             result.command.kind == DepotCommandKind::FileDownload ||
             result.command.kind == DepotCommandKind::AllFilesDownload) &&
            arg == "--depot-key" && index + 1 < argc) {
            result.command.depot_key_hex = argv[++index];
            continue;
        }
        if (result.command.kind == DepotCommandKind::VerifyLocal &&
            arg == "--local-root" && index + 1 < argc) {
            result.command.local_root = argv[++index];
            continue;
        }
        if ((result.command.kind == DepotCommandKind::ChunkDownload ||
             result.command.kind == DepotCommandKind::FileDownload) &&
            arg == "--file-index" && index + 1 < argc) {
            result.command.file_index = static_cast<std::size_t>(std::max(0, std::atoi(argv[++index])));
            result.command.has_file_index = true;
            continue;
        }
        if ((result.command.kind == DepotCommandKind::ChunkDownload ||
             result.command.kind == DepotCommandKind::FileDownload) &&
            arg == "--file" && index + 1 < argc) {
            result.command.file_path = argv[++index];
            continue;
        }
        if (result.command.kind == DepotCommandKind::FileList &&
            arg == "--filter" && index + 1 < argc) {
            result.command.filter_text = argv[++index];
            continue;
        }
        if (result.command.kind == DepotCommandKind::FileList &&
            arg == "--limit" && index + 1 < argc) {
            result.command.list_limit = static_cast<std::size_t>(std::max(1, std::atoi(argv[++index])));
            continue;
        }
        if (result.command.kind == DepotCommandKind::ChunkDownload &&
            arg == "--chunk-index" && index + 1 < argc) {
            result.command.chunk_index = static_cast<std::size_t>(std::max(0, std::atoi(argv[++index])));
            result.command.has_chunk_index = true;
            continue;
        }
        if (result.command.kind == DepotCommandKind::ChunkDownload && arg == "--process") {
            result.command.process_chunk = true;
            continue;
        }
        if (result.command.kind == DepotCommandKind::ManifestDownload ||
            result.command.kind == DepotCommandKind::ChunkDownload ||
            result.command.kind == DepotCommandKind::FileDownload ||
            result.command.kind == DepotCommandKind::AllFilesDownload) {
            const auto write_flag_result = apply_write_mode_flag(
                result.command.write_options, result.command.has_write_mode, arg, std::cerr);
            if (write_flag_result < 0) {
                return result;
            }
            if (write_flag_result > 0) {
                continue;
            }
        }
        if ((result.command.kind == DepotCommandKind::Branches ||
             result.command.kind == DepotCommandKind::Manifests ||
             result.command.kind == DepotCommandKind::Key ||
             result.command.kind == DepotCommandKind::Preflight ||
             result.command.kind == DepotCommandKind::ManifestCode ||
             result.command.kind == DepotCommandKind::ManifestDownload ||
             result.command.kind == DepotCommandKind::ChunkDownload ||
             result.command.kind == DepotCommandKind::FileDownload ||
             result.command.kind == DepotCommandKind::AllFilesDownload ||
             result.command.kind == DepotCommandKind::Routes) &&
            arg == "--route-endpoint" && index + 1 < argc) {
            result.command.route_selection.endpoint = argv[++index];
            continue;
        }
        if ((result.command.kind == DepotCommandKind::Branches ||
             result.command.kind == DepotCommandKind::Manifests ||
             result.command.kind == DepotCommandKind::Key ||
             result.command.kind == DepotCommandKind::Preflight ||
             result.command.kind == DepotCommandKind::ManifestCode ||
             result.command.kind == DepotCommandKind::ManifestDownload ||
             result.command.kind == DepotCommandKind::ChunkDownload ||
             result.command.kind == DepotCommandKind::FileDownload ||
             result.command.kind == DepotCommandKind::AllFilesDownload ||
             result.command.kind == DepotCommandKind::Routes) &&
            arg == "--route-protocol" && index + 1 < argc) {
            result.command.route_selection.protocol = argv[++index];
            continue;
        }
        if ((result.command.kind == DepotCommandKind::ManifestDownload ||
             result.command.kind == DepotCommandKind::ChunkDownload ||
             result.command.kind == DepotCommandKind::FileDownload ||
             result.command.kind == DepotCommandKind::AllFilesDownload) &&
            arg == "--atomic-write") {
            result.command.write_options.atomic_write = true;
            continue;
        }
        if ((result.command.kind == DepotCommandKind::ManifestDownload ||
             result.command.kind == DepotCommandKind::ChunkDownload ||
             result.command.kind == DepotCommandKind::FileDownload ||
             result.command.kind == DepotCommandKind::AllFilesDownload) &&
            arg == "--no-atomic-write") {
            result.command.write_options.atomic_write = false;
            continue;
        }
        if ((result.command.kind == DepotCommandKind::Manifests ||
             result.command.kind == DepotCommandKind::Preflight ||
             result.command.kind == DepotCommandKind::ManifestCode) &&
            arg == "--branch" && index + 1 < argc) {
            result.command.branch = argv[++index];
            continue;
        }
        std::cerr << "unknown or incomplete steam depot option: " << arg << '\n';
        cauth::cli::print_cli_usage();
        return result;
    }

    const auto requires_app_id =
        result.command.kind != DepotCommandKind::Routes &&
        result.command.kind != DepotCommandKind::ManifestDownload &&
        result.command.kind != DepotCommandKind::ManifestInfo &&
        result.command.kind != DepotCommandKind::FileList &&
        result.command.kind != DepotCommandKind::VerifyLocal &&
        result.command.kind != DepotCommandKind::ChunkDownload &&
        result.command.kind != DepotCommandKind::FileDownload &&
        result.command.kind != DepotCommandKind::AllFilesDownload;
    if (requires_app_id && result.command.app_id == 0) {
        std::cerr << "steam depot " << subcommand << " requires --app-id <id>\n";
        return result;
    }
    const auto requires_steam_id =
        result.command.kind == DepotCommandKind::Branches ||
        result.command.kind == DepotCommandKind::Manifests ||
        result.command.kind == DepotCommandKind::Key ||
        result.command.kind == DepotCommandKind::Preflight ||
        result.command.kind == DepotCommandKind::ManifestCode;
    if (requires_steam_id && result.command.steam_id == 0) {
        std::cerr << "steam depot " << subcommand << " requires --steam-id <id>\n";
        return result;
    }
    if ((result.command.kind == DepotCommandKind::ManifestInfo ||
         result.command.kind == DepotCommandKind::FileList ||
         result.command.kind == DepotCommandKind::VerifyLocal ||
         result.command.kind == DepotCommandKind::ChunkDownload ||
         result.command.kind == DepotCommandKind::FileDownload ||
         result.command.kind == DepotCommandKind::AllFilesDownload) &&
        result.command.input_path.empty()) {
        std::cerr << "steam depot " << subcommand << " requires --in <path>\n";
        return result;
    }
    if (result.command.kind == DepotCommandKind::VerifyLocal &&
        result.command.local_root.empty()) {
        std::cerr << "steam depot verify-local requires --local-root <path>\n";
        return result;
    }
    if (result.command.kind == DepotCommandKind::Key && result.command.depot_id == 0) {
        std::cerr << "steam depot key requires --depot-id <id>\n";
        return result;
    }
    if (result.command.kind == DepotCommandKind::ManifestCode &&
        (result.command.depot_id == 0 || result.command.manifest_gid == 0)) {
        std::cerr << "steam depot manifest-code requires --depot-id <id> --manifest-gid <gid>\n";
        return result;
    }
    if (result.command.kind == DepotCommandKind::ManifestDownload &&
        (result.command.depot_id == 0 || result.command.manifest_gid == 0 ||
         result.command.request_code == 0 || result.command.output_path.empty())) {
        std::cerr << "steam depot manifest-download requires --depot-id <id> --manifest-gid <gid> --request-code <code> --out <path>\n";
        return result;
    }
    if (result.command.kind == DepotCommandKind::ChunkDownload &&
        ((!result.command.has_file_index && result.command.file_path.empty()) ||
         !result.command.has_chunk_index || result.command.output_path.empty())) {
        std::cerr << "steam depot chunk-download requires (--file-index <index>|--file <path>) --chunk-index <index> --out <path>\n";
        return result;
    }
    if (result.command.kind == DepotCommandKind::ChunkDownload &&
        result.command.process_chunk && result.command.depot_key_hex.empty()) {
        std::cerr << "steam depot chunk-download --process requires --depot-key <hex>\n";
        return result;
    }
    if (result.command.kind == DepotCommandKind::FileDownload &&
        ((!result.command.has_file_index && result.command.file_path.empty()) ||
         result.command.output_path.empty() ||
         result.command.depot_key_hex.empty())) {
        std::cerr << "steam depot file-download requires (--file-index <index>|--file <path>) --depot-key <hex> --out <path>\n";
        return result;
    }
    if (result.command.kind == DepotCommandKind::AllFilesDownload &&
        (result.command.output_path.empty() || result.command.depot_key_hex.empty())) {
        std::cerr << "steam depot all-files-download requires --depot-key <hex> --out-dir <path>\n";
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace

namespace cauth::cli {

int run_steam_depot(int argc, char** argv) {
    const auto parsed = parse_depot_command(argc, argv);
    if (!parsed.ok) return parsed.exit_code;

    const auto& request = parsed.command;
    const bool enable_progress =
        request.kind == DepotCommandKind::ManifestDownload ||
        request.kind == DepotCommandKind::ChunkDownload ||
        request.kind == DepotCommandKind::FileDownload ||
        request.kind == DepotCommandKind::AllFilesDownload ||
        request.kind == DepotCommandKind::VerifyLocal;
    ScopedDepotCliProgress scoped_progress{enable_progress, request.pause_on_sigint};

    if (request.kind == DepotCommandKind::Routes) {
        return cauth::steam::depot::print_download_routes(
            request.max_count,
            request.route_selection.empty() ? nullptr : &request.route_selection,
            std::cout,
            std::cerr);
    }
    if (request.kind == DepotCommandKind::Branches) {
        return cauth::steam::depot::print_branches(
            request.steam_id,
            request.app_id,
            request.max_count,
            request.route_selection.empty() ? nullptr : &request.route_selection,
            std::cout,
            std::cerr);
    }
    if (request.kind == DepotCommandKind::Manifests) {
        return cauth::steam::depot::print_manifests(
            request.steam_id,
            request.app_id,
            request.branch,
            request.max_count,
            request.route_selection.empty() ? nullptr : &request.route_selection,
            std::cout,
            std::cerr);
    }
    if (request.kind == DepotCommandKind::Preflight) {
        return cauth::steam::depot::print_preflight(
            request.steam_id,
            request.app_id,
            request.branch,
            request.max_count,
            request.route_selection.empty() ? nullptr : &request.route_selection,
            std::cout,
            std::cerr);
    }
    if (request.kind == DepotCommandKind::Key) {
        return cauth::steam::depot::print_depot_key(
            request.steam_id,
            request.app_id,
            request.depot_id,
            request.max_count,
            request.route_selection.empty() ? nullptr : &request.route_selection,
            std::cout,
            std::cerr);
    }
    if (request.kind == DepotCommandKind::ManifestCode) {
        return cauth::steam::depot::print_manifest_request_code(
            request.steam_id,
            request.app_id,
            request.depot_id,
            request.manifest_gid,
            request.branch,
            request.max_count,
            request.route_selection.empty() ? nullptr : &request.route_selection,
            std::cout,
            std::cerr);
    }
    if (request.kind == DepotCommandKind::ManifestDownload) {
        return cauth::steam::depot::download_manifest_to_path(
            request.depot_id,
            request.manifest_gid,
            request.request_code,
            request.max_count,
            request.output_path,
            request.route_selection.empty() ? nullptr : &request.route_selection,
            request.write_options,
            std::cout,
            std::cerr);
    }

    std::optional<std::vector<std::uint8_t>> depot_key;
    if (!request.depot_key_hex.empty()) {
        depot_key = hex_to_bytes(request.depot_key_hex);
        if (!depot_key.has_value()) {
            std::cerr << "Invalid depot key hex\n";
            return 1;
        }
    }

    cauth::steam::depot::LoadedDepotManifest loaded_manifest;
    if (!cauth::steam::depot::load_manifest_from_path(
            request.input_path, depot_key, loaded_manifest, std::cerr)) {
        return 1;
    }

    if (request.kind == DepotCommandKind::FileList) {
        return cauth::steam::depot::print_file_list(
            loaded_manifest, request.filter_text, request.list_limit, std::cout);
    }
    if (request.kind == DepotCommandKind::VerifyLocal) {
        return cauth::steam::depot::verify_local_files_against_manifest(
            loaded_manifest,
            request.local_root,
            request.filter_text,
            std::cout,
            std::cerr);
    }
    if (request.kind == DepotCommandKind::ManifestInfo) {
        return cauth::steam::depot::print_manifest_info(loaded_manifest, std::cout);
    }
    if (request.kind == DepotCommandKind::AllFilesDownload) {
        return cauth::steam::depot::download_all_files_from_manifest(
            loaded_manifest,
            request.max_count,
            request.output_path,
            request.route_selection.empty() ? nullptr : &request.route_selection,
            request.write_options,
            std::cout,
            std::cerr);
    }

    const auto selected_file_index = cauth::steam::depot::resolve_file_selection(
        loaded_manifest,
        request.file_index,
        request.has_file_index,
        request.file_path,
        std::cerr);
    if (!selected_file_index.has_value()) return 2;

    if (request.kind == DepotCommandKind::ChunkDownload &&
        !cauth::steam::depot::validate_chunk_selection(
            loaded_manifest,
            *selected_file_index,
            request.chunk_index,
            std::cerr)) {
        return 2;
    }

    if (request.kind == DepotCommandKind::ChunkDownload) {
        return cauth::steam::depot::download_chunk_from_manifest(
            loaded_manifest,
            *selected_file_index,
            request.chunk_index,
            request.process_chunk,
            request.max_count,
            request.output_path,
            request.route_selection.empty() ? nullptr : &request.route_selection,
            request.write_options,
            std::cout,
            std::cerr);
    }

    return cauth::steam::depot::download_file_from_manifest(
        loaded_manifest,
        *selected_file_index,
        request.max_count,
        request.output_path,
        request.route_selection.empty() ? nullptr : &request.route_selection,
        request.write_options,
        std::cout,
        std::cerr);
}

} // namespace cauth::cli
