#include "cli/steam_cli.hpp"
#include "cli/cli_support.hpp"

#include "core/platform/session_repository_factory.hpp"
#include "steam/cloud/steam_cloud_application.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace {
using namespace cauth::cli::support;

enum class CloudCommandKind {
    List,
    Verify,
    Pull,
    Push,
};

struct ParsedCloudCommand {
    CloudCommandKind kind = CloudCommandKind::List;
    cauth::steam::cloud::SteamCloudRequest request;
    std::uint32_t count = 100;
    std::uint32_t start_index = 0;
    bool extended_details = true;
    bool include_extra_local = false;
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
    if (subcommand == "list") {
        result.command.kind = CloudCommandKind::List;
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

        const auto store = cauth::core::platform::make_platform_session_repository();
        switch (parsed.command.kind) {
        case CloudCommandKind::List:
            return cauth::steam::cloud::print_remote_files(
                *store,
                parsed.command.request,
                parsed.command.count,
                parsed.command.start_index,
                parsed.command.extended_details,
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
