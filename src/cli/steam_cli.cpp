#include "application/cauth_application.hpp"
#include "cli/steam_cli.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cauth::cli {
namespace {

using CommandHandler = int (*)(int argc, char** argv, std::ostream& out, std::ostream& err);

std::optional<cauth::core::platform::SessionRepositoryOptions> g_cli_session_options;

int run_version_command(int /*argc*/, char** /*argv*/, std::ostream& out, std::ostream& /*err*/) {
    return cauth::application::print_version(out);
}

int run_doctor_command(int /*argc*/, char** /*argv*/, std::ostream& out, std::ostream& err) {
    return cauth::application::run_doctor(out, err);
}

int run_steam_command(int argc, char** argv, std::ostream& /*out*/, std::ostream& err) {
    if (argc < 3) {
        err << "missing steam command\n";
        print_cli_usage();
        return 2;
    }

    const std::string_view steam_command = argv[2];
    if (steam_command == "auth") return run_steam_auth(argc - 1, argv + 1);
    if (steam_command == "depot") return run_steam_depot(argc - 1, argv + 1);
    if (steam_command == "cloud") return run_steam_cloud(argc - 1, argv + 1);

    err << "unknown steam command: " << steam_command << '\n';
    print_cli_usage();
    return 2;
}

int run_microsoft_command(int /*argc*/, char** /*argv*/, std::ostream& /*out*/, std::ostream& err) {
    err << "microsoft provider commands are not implemented yet\n";
    return 2;
}

struct CommandEntry {
    std::string_view name;
    CommandHandler handler;
};

struct ScopedCliSessionRepositoryOptions {
    explicit ScopedCliSessionRepositoryOptions(
        const std::optional<cauth::core::platform::SessionRepositoryOptions>& options) {
        if (!options.has_value()) {
            return;
        }
        cauth::core::platform::set_current_thread_session_repository_options(*options);
        active_ = true;
    }

    ~ScopedCliSessionRepositoryOptions() {
        if (active_) {
            cauth::core::platform::clear_current_thread_session_repository_options();
        }
    }

  private:
    bool active_ = false;
};

constexpr CommandEntry kTopLevelCommands[] = {
    {"--version", &run_version_command},
    {"-v", &run_version_command},
    {"doctor", &run_doctor_command},
    {"steam", &run_steam_command},
    {"microsoft", &run_microsoft_command},
};

} // namespace

void print_cli_usage() {
    std::cout << "CAuth\n"
              << "\n"
              << "Usage:\n"
              << "  cauth [--session-store <path>|--memory-session-store] <command> ...\n"
              << "  cauth --version\n"
              << "  cauth doctor\n"
              << "  cauth steam auth login --username <name> --password-stdin [--guard-code <code>] [--poll-attempts <count>] [--device-name <caller name>] [--cm-max-count <count>] [--route-endpoint <host:port>] [--route-protocol websocket]\n"
              << "  cauth steam auth login-web --username <name> --password-stdin [--guard-code <code>] [--poll-attempts <count>] [--device-name <caller name>]\n"
              << "  cauth steam auth login-mobile --username <name> --password-stdin [--guard-code <code>] [--poll-attempts <count>] [--device-name <caller name>]\n"
              << "  cauth steam auth whoami --steam-id <id>\n"
              << "  cauth steam auth accounts\n"
              << "  cauth steam auth refresh-access --steam-id <id>\n"
              << "  cauth steam auth web-cookies --steam-id <id>\n"
              << "  cauth steam auth token-info --steam-id <id>\n"
              << "  cauth steam auth status\n"
              << "  cauth steam auth clear (--steam-id <id>|--all)\n"
              << "  cauth steam auth cm servers [--protocol websocket|tcp] [--max-count <count>]\n"
              << "  cauth steam auth cm routes [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol websocket]\n"
              << "  cauth steam auth cm probe [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol websocket]\n"
              << "  cauth steam auth cm logon --steam-id <id> [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol websocket]\n"
              << "  cauth steam auth cm app-info --steam-id <id> --app-id <id> [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol websocket]\n"
              << "  cauth steam auth cm frame-test\n"
              << "  cauth steam depot routes [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol https|http]\n"
              << "  cauth steam depot branches --steam-id <id> --app-id <id> [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol websocket]\n"
              << "  cauth steam depot manifests --steam-id <id> --app-id <id> --branch <name> [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol websocket]\n"
              << "  cauth steam depot key --steam-id <id> --app-id <id> --depot-id <id> [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol websocket]\n"
              << "  cauth steam depot preflight --steam-id <id> --app-id <id> --branch <name> [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol websocket]\n"
              << "  cauth steam depot manifest-code --steam-id <id> --app-id <id> --depot-id <id> --manifest-gid <gid> [--branch <name>] [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol websocket]\n"
              << "  cauth steam depot manifest-download --depot-id <id> --manifest-gid <gid> --request-code <code> --out <path> [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol https|http] [--overwrite|--skip-existing|--fail-if-exists] [--no-atomic-write]\n"
              << "  cauth steam depot manifest-info --in <path> [--depot-key <hex>]\n"
              << "  cauth steam depot file-list --in <manifest> [--depot-key <hex>] [--filter <text>] [--limit <count>]\n"
              << "  cauth steam depot verify-local --in <manifest> --local-root <path> [--depot-key <hex>] [--filter <text>]\n"
              << "  cauth steam depot chunk-download --in <manifest> (--file-index <index>|--file <path>) --chunk-index <index> --out <path> [--depot-key <hex>] [--process] [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol https|http] [--overwrite|--skip-existing|--fail-if-exists] [--no-atomic-write]\n"
              << "  cauth steam depot file-download --in <manifest> (--file-index <index>|--file <path>) --depot-key <hex> --out <path> [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol https|http] [--overwrite|--skip-existing|--fail-if-exists] [--no-atomic-write]\n"
              << "  cauth steam depot all-files-download --in <manifest> --depot-key <hex> --out-dir <path> [--max-count <count>] [--route-endpoint <host:port>] [--route-protocol https|http] [--overwrite|--skip-existing|--fail-if-exists] [--no-atomic-write]\n"
              << "  cauth steam cloud routes --steam-id <id> --app-id <id> [--backend auto|cm|web] [--task list|verify|pull|push] [--count <count>] [--route-endpoint <host:port>] [--route-protocol websocket|https|http] [--route-role <role>]\n"
              << "  cauth steam cloud list --steam-id <id> --app-id <id> [--backend auto|cm|web] [--access-token <token>] [--remote-root <path>] [--count <count>] [--start-index <index>] [--extended-details 0|1] [--route-endpoint <host:port>] [--route-protocol websocket|https|http] [--route-role <role>]\n"
              << "  cauth steam cloud web-page-list --steam-id <id> --app-id <id> [--remote-root <path>] [--count <count>] [--start-index <index>] [--route-endpoint <host:port>] [--route-protocol https|http] [--route-role <role>]\n"
              << "  cauth steam cloud verify --steam-id <id> --app-id <id> --local-root <path> [--backend auto|cm|web] [--access-token <token>] [--remote-root <path>] [--include-extra-local] [--extended-details 0|1] [--route-endpoint <host:port>] [--route-protocol websocket|https|http] [--route-role <role>]\n"
              << "  cauth steam cloud pull --steam-id <id> --app-id <id> --local-root <path> [--backend auto|cm|web] [--access-token <token>] [--remote-root <path>] [--dry-run] [--conflict-policy default|local-wins|remote-wins|newer-wins|fail] [--route-endpoint <host:port>] [--route-protocol websocket|https|http] [--route-role <role>] [--overwrite|--skip-existing|--fail-if-exists] [--no-atomic-write]\n"
              << "  cauth steam cloud push --steam-id <id> --app-id <id> --local-root <path> [--backend auto|cm|web] [--access-token <token>] [--remote-root <path>] [--dry-run] [--delete-remote-orphans] [--conflict-policy default|local-wins|remote-wins|newer-wins|fail] [--route-endpoint <host:port>] [--route-protocol websocket|https|http] [--route-role <role>]\n"
              << "    Note: Steam Cloud web backend is currently unsupported; use `steam auth login` and the CM backend.\n"
              << "    Note: `steam cloud web-page-list` is diagnostic only and does not mean web pull/push is usable.\n";
}

int run_cli(int argc, char** argv, std::ostream& out, std::ostream& err) {
    if (argc <= 1) {
        print_cli_usage();
        return 0;
    }

    std::vector<char*> filtered_argv;
    filtered_argv.reserve(static_cast<std::size_t>(argc));
    filtered_argv.push_back(argv[0]);

    std::optional<cauth::core::platform::SessionRepositoryOptions> session_options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--session-store" && index + 1 < argc) {
            if (!session_options.has_value()) {
                session_options = cauth::core::platform::SessionRepositoryOptions{};
            }
            session_options->backend = cauth::core::platform::SessionRepositoryBackend::File;
            session_options->storage_path = argv[++index];
            continue;
        }
        if (arg == "--memory-session-store") {
            if (!session_options.has_value()) {
                session_options = cauth::core::platform::SessionRepositoryOptions{};
            }
            session_options->backend = cauth::core::platform::SessionRepositoryBackend::Memory;
            session_options->storage_path.clear();
            continue;
        }
        filtered_argv.push_back(argv[index]);
    }

    if (filtered_argv.size() <= 1) {
        print_cli_usage();
        return 0;
    }

    g_cli_session_options = session_options;
    ScopedCliSessionRepositoryOptions scoped_session_options{session_options};
    const std::string_view command = filtered_argv[1];
    for (const auto& entry : kTopLevelCommands) {
        if (entry.name == command) {
            const auto exit_code = entry.handler(
                static_cast<int>(filtered_argv.size()), filtered_argv.data(), out, err);
            g_cli_session_options.reset();
            return exit_code;
        }
    }

    err << "unknown command: " << command << '\n';
    print_cli_usage();
    g_cli_session_options.reset();
    return 2;
}

const cauth::core::platform::SessionRepositoryOptions* current_cli_session_repository_options() {
    return g_cli_session_options.has_value() ? &*g_cli_session_options : nullptr;
}

} // namespace cauth::cli
