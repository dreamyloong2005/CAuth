#include "cli/steam_cli.hpp"
#include "cli/cli_support.hpp"

#include "core/platform/session_repository_factory.hpp"
#include "steam/cloud/steam_cloud_application.hpp"

#include <atomic>
#include <csignal>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace {
using namespace cauth::cli::support;

std::atomic_bool g_cloud_cli_cancel_requested{false};
std::atomic_bool g_cloud_cli_pause_requested{false};
std::atomic_bool g_cloud_cli_pause_on_sigint{false};

void on_cloud_cli_signal(int) {
    if (g_cloud_cli_pause_on_sigint.load()) {
        g_cloud_cli_pause_requested.store(true);
    } else {
        g_cloud_cli_cancel_requested.store(true);
    }
}

bool cloud_cli_cancel_requested(void*) {
    return g_cloud_cli_cancel_requested.load();
}

bool cloud_cli_pause_requested(void*) {
    return g_cloud_cli_pause_requested.load();
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

struct CloudCliProgressState {
    ProgressLineState line_state;
};

void on_cloud_progress(const cauth::steam::cloud::SteamCloudTransferProgress& progress,
                       void* user_data) {
    auto* state = static_cast<CloudCliProgressState*>(user_data);
    if (state == nullptr) {
        return;
    }

    std::string line = "steam cloud";
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

struct ScopedCloudCliProgress {
    explicit ScopedCloudCliProgress(bool enabled, bool pause_on_sigint)
        : enabled_(enabled), pause_on_sigint_(pause_on_sigint) {
        g_cloud_cli_cancel_requested.store(false);
        g_cloud_cli_pause_requested.store(false);
        g_cloud_cli_pause_on_sigint.store(pause_on_sigint_);
        previous_handler_ = std::signal(SIGINT, &on_cloud_cli_signal);
        cauth::steam::cloud::set_current_thread_steam_cloud_transfer_hooks(
            enabled_ ? &on_cloud_progress : nullptr,
            &cloud_cli_cancel_requested,
            &cloud_cli_pause_requested,
            &state_);
    }

    ~ScopedCloudCliProgress() {
        cauth::steam::cloud::clear_current_thread_steam_cloud_transfer_hooks();
        if (enabled_) {
            finish_progress_line(std::cerr, state_.line_state);
        }
        g_cloud_cli_pause_on_sigint.store(false);
        std::signal(SIGINT, previous_handler_);
    }

  private:
    bool enabled_ = false;
    bool pause_on_sigint_ = false;
    CloudCliProgressState state_;
    void (*previous_handler_)(int) = SIG_DFL;
};

enum class CloudCommandKind {
    Routes,
    List,
    WebPageList,
    Verify,
    Pull,
    Push,
};

std::optional<cauth::steam::cloud::SteamCloudRouteTask> parse_cloud_route_task(
    std::string_view value) {
    using Task = cauth::steam::cloud::SteamCloudRouteTask;
    if (value == "list") return Task::List;
    if (value == "verify") return Task::Verify;
    if (value == "pull") return Task::Pull;
    if (value == "push") return Task::Push;
    return std::nullopt;
}

struct ParsedCloudCommand {
    CloudCommandKind kind = CloudCommandKind::List;
    cauth::steam::cloud::SteamCloudRequest request;
    cauth::steam::cloud::SteamCloudRouteTask route_task =
        cauth::steam::cloud::SteamCloudRouteTask::Pull;
    std::uint32_t count = 100;
    std::uint32_t start_index = 0;
    bool extended_details = true;
    bool include_extra_local = false;
    bool has_write_mode = false;
    bool pause_on_sigint = false;
};

struct ParsedCloudResult {
    bool ok = false;
    int exit_code = 2;
    ParsedCloudCommand command;
};

std::optional<cauth::steam::cloud::SteamCloudConflictPolicy> parse_conflict_policy(
    std::string_view value) {
    using Policy = cauth::steam::cloud::SteamCloudConflictPolicy;
    if (value == "default") return Policy::Default;
    if (value == "local-wins") return Policy::LocalWins;
    if (value == "remote-wins") return Policy::RemoteWins;
    if (value == "newer-wins") return Policy::NewerWins;
    if (value == "fail" || value == "fail-on-conflict") return Policy::FailOnConflict;
    return std::nullopt;
}

std::optional<cauth::steam::cloud::SteamCloudBackend> parse_cloud_backend(std::string_view value) {
    using Backend = cauth::steam::cloud::SteamCloudBackend;
    if (value == "auto") return Backend::Auto;
    if (value == "web" || value == "web-api") return Backend::WebApi;
    if (value == "cm" || value == "cm-cloud") return Backend::CmCloud;
    return std::nullopt;
}

ParsedCloudResult parse_cloud_command(int argc, char** argv) {
    ParsedCloudResult result;
    if (argc < 3) {
        std::cerr << "missing steam cloud command\n";
        cauth::cli::print_cli_usage();
        return result;
    }

    const std::string_view subcommand = argv[2];
    if (subcommand == "routes") {
        result.command.kind = CloudCommandKind::Routes;
    } else if (subcommand == "list") {
        result.command.kind = CloudCommandKind::List;
    } else if (subcommand == "web-page-list") {
        result.command.kind = CloudCommandKind::WebPageList;
    } else if (subcommand == "verify") {
        result.command.kind = CloudCommandKind::Verify;
    } else if (subcommand == "pull") {
        result.command.kind = CloudCommandKind::Pull;
    } else if (subcommand == "push") {
        result.command.kind = CloudCommandKind::Push;
    } else {
        std::cerr << "unknown steam cloud command: " << subcommand << '\n';
        cauth::cli::print_cli_usage();
        return result;
    }

    for (int index = 3; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--app-id" && index + 1 < argc) {
            result.command.request.app_id = static_cast<std::uint32_t>(std::max(0, std::atoi(argv[++index])));
            continue;
        }
        if (arg == "--access-token" && index + 1 < argc) {
            result.command.request.access_token = argv[++index];
            continue;
        }
        if (arg == "--refresh-token" && index + 1 < argc) {
            result.command.request.refresh_token = argv[++index];
            continue;
        }
        if (arg == "--steam-id" && index + 1 < argc) {
            result.command.request.steam_id =
                static_cast<std::uint64_t>(std::strtoull(argv[++index], nullptr, 10));
            continue;
        }
        if (arg == "--local-root" && index + 1 < argc) {
            result.command.request.local_root = argv[++index];
            continue;
        }
        if (arg == "--remote-root" && index + 1 < argc) {
            result.command.request.remote_root = argv[++index];
            continue;
        }
        if (arg == "--dry-run") {
            result.command.request.dry_run = true;
            continue;
        }
        if (arg == "--delete-remote-orphans") {
            result.command.request.delete_remote_orphans = true;
            continue;
        }
        if (arg == "--count" && index + 1 < argc) {
            result.command.count = static_cast<std::uint32_t>(std::max(1, std::atoi(argv[++index])));
            continue;
        }
        if (arg == "--start-index" && index + 1 < argc) {
            result.command.start_index = static_cast<std::uint32_t>(std::max(0, std::atoi(argv[++index])));
            continue;
        }
        if (arg == "--extended-details" && index + 1 < argc) {
            result.command.extended_details = std::atoi(argv[++index]) != 0;
            continue;
        }
        if (arg == "--include-extra-local") {
            result.command.include_extra_local = true;
            continue;
        }
        if (arg == "--conflict-policy" && index + 1 < argc) {
            const auto parsed = parse_conflict_policy(argv[++index]);
            if (!parsed.has_value()) {
                std::cerr << "unknown steam cloud conflict policy\n";
                return result;
            }
            result.command.request.conflict_policy = *parsed;
            continue;
        }
        if (arg == "--backend" && index + 1 < argc) {
            const auto parsed = parse_cloud_backend(argv[++index]);
            if (!parsed.has_value()) {
                std::cerr << "unknown steam cloud backend\n";
                return result;
            }
            result.command.request.backend = *parsed;
            continue;
        }
        if ((result.command.kind == CloudCommandKind::Pull ||
             result.command.kind == CloudCommandKind::Push ||
             result.command.kind == CloudCommandKind::Verify) &&
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
        if (arg == "--route-endpoint" && index + 1 < argc) {
            result.command.request.route_selection.endpoint = argv[++index];
            continue;
        }
        if (arg == "--route-protocol" && index + 1 < argc) {
            result.command.request.route_selection.protocol = argv[++index];
            continue;
        }
        if (arg == "--route-role" && index + 1 < argc) {
            result.command.request.route_selection.role = argv[++index];
            continue;
        }
        if (result.command.kind == CloudCommandKind::Routes &&
            arg == "--task" && index + 1 < argc) {
            const auto parsed = parse_cloud_route_task(argv[++index]);
            if (!parsed.has_value()) {
                std::cerr << "unknown steam cloud route task\n";
                return result;
            }
            result.command.route_task = *parsed;
            continue;
        }
        if (result.command.kind == CloudCommandKind::Pull) {
            const auto write_flag_result = apply_write_mode_flag(
                result.command.request.local_write_options,
                result.command.has_write_mode,
                arg,
                std::cerr);
            if (write_flag_result < 0) {
                return result;
            }
            if (write_flag_result > 0) {
                continue;
            }
            if (arg == "--atomic-write") {
                result.command.request.local_write_options.atomic_write = true;
                continue;
            }
            if (arg == "--no-atomic-write") {
                result.command.request.local_write_options.atomic_write = false;
                continue;
            }
        } else if (result.command.kind == CloudCommandKind::Push &&
                   (arg == "--overwrite" || arg == "--skip-existing" ||
                    arg == "--fail-if-exists" || arg == "--atomic-write" ||
                    arg == "--no-atomic-write")) {
            std::cerr << "steam cloud push does not support local write options\n";
            return result;
        }
        std::cerr << "unknown or incomplete steam cloud option: " << arg << '\n';
        cauth::cli::print_cli_usage();
        return result;
    }

    if (result.command.request.app_id == 0) {
        std::cerr << "steam cloud " << subcommand << " requires --app-id <id>\n";
        return result;
    }
    if (result.command.request.steam_id == 0) {
        std::cerr << "steam cloud " << subcommand << " requires --steam-id <id>\n";
        return result;
    }
    if ((result.command.kind == CloudCommandKind::Verify ||
         result.command.kind == CloudCommandKind::Pull ||
         result.command.kind == CloudCommandKind::Push) &&
        result.command.request.local_root.empty()) {
        std::cerr << "steam cloud " << subcommand << " requires --local-root <path>\n";
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace

namespace cauth::cli {

int run_steam_cloud(int argc, char** argv) {
    try {
        const auto parsed = parse_cloud_command(argc, argv);
        if (!parsed.ok) return parsed.exit_code;

        const bool enable_progress =
            parsed.command.kind == CloudCommandKind::Verify ||
            parsed.command.kind == CloudCommandKind::Pull ||
            parsed.command.kind == CloudCommandKind::Push;
        ScopedCloudCliProgress scoped_progress{enable_progress, parsed.command.pause_on_sigint};
        const auto store = cauth::core::platform::make_platform_session_repository();
        switch (parsed.command.kind) {
        case CloudCommandKind::Routes:
            return cauth::steam::cloud::print_cloud_routes(
                *store,
                parsed.command.request,
                parsed.command.route_task,
                parsed.command.count,
                std::cout,
                std::cerr);
        case CloudCommandKind::List:
            return cauth::steam::cloud::print_remote_files(
                *store,
                parsed.command.request,
                parsed.command.count,
                parsed.command.start_index,
                parsed.command.extended_details,
                std::cout,
                std::cerr);
        case CloudCommandKind::WebPageList:
            return cauth::steam::cloud::print_remote_files_via_web_page_diagnostic(
                *store,
                parsed.command.request,
                parsed.command.count,
                parsed.command.start_index,
                std::cout,
                std::cerr);
        case CloudCommandKind::Verify:
            return cauth::steam::cloud::run_verify_cloud_local(
                *store,
                parsed.command.request,
                parsed.command.include_extra_local,
                std::cout,
                std::cerr);
        case CloudCommandKind::Pull:
            return cauth::steam::cloud::run_pull_cloud_save(
                *store,
                parsed.command.request,
                std::cout,
                std::cerr);
        case CloudCommandKind::Push:
            return cauth::steam::cloud::run_push_cloud_save(
                *store,
                parsed.command.request,
                std::cout,
                std::cerr);
        }
    } catch (const std::exception& ex) {
        std::cerr << "Steam cloud failed: " << ex.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Steam cloud failed: unknown exception\n";
        return 1;
    }
    return 2;
}

} // namespace cauth::cli
