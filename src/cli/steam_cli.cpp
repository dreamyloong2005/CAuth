#include "application/cauth_application.hpp"
#include "cli/steam_cli.hpp"

#include <iostream>
#include <string_view>

namespace cauth::cli {
namespace {

using CommandHandler = int (*)(int argc, char** argv, std::ostream& out, std::ostream& err);

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
              << "  cauth --version\n"
              << "  cauth doctor\n"
              << "  cauth steam auth login --username <name> --password-stdin [--guard-code <code>] [--poll-attempts <count>] [--device-name <caller name>] [--cm-max-count <count>]\n"
              << "  cauth steam auth login-web --username <name> --password-stdin [--guard-code <code>] [--poll-attempts <count>] [--device-name <caller name>]\n"
              << "  cauth steam auth login-mobile --username <name> --password-stdin [--guard-code <code>] [--poll-attempts <count>] [--device-name <caller name>]\n"
              << "  cauth steam auth whoami\n"
              << "  cauth steam auth refresh-access\n"
              << "  cauth steam auth web-cookies\n"
              << "  cauth steam auth token-info\n"
              << "  cauth steam auth status\n"
              << "  cauth steam auth clear\n"
              << "  cauth steam auth cm servers [--protocol websocket|tcp] [--max-count <count>]\n"
              << "  cauth steam auth cm probe [--max-count <count>]\n"
              << "  cauth steam auth cm logon [--max-count <count>]\n"
              << "  cauth steam auth cm app-info --app-id <id> [--max-count <count>]\n"
              << "  cauth steam auth cm frame-test\n"
              << "  cauth steam depot branches --app-id <id> [--max-count <count>]\n"
              << "  cauth steam depot manifests --app-id <id> --branch <name> [--max-count <count>]\n"
              << "  cauth steam depot key --app-id <id> --depot-id <id> [--max-count <count>]\n"
              << "  cauth steam depot preflight --app-id <id> --branch <name> [--max-count <count>]\n"
              << "  cauth steam depot manifest-code --app-id <id> --depot-id <id> --manifest-gid <gid> [--branch <name>] [--max-count <count>]\n"
              << "  cauth steam depot manifest-download --depot-id <id> --manifest-gid <gid> --request-code <code> --out <path> [--max-count <count>]\n"
              << "  cauth steam depot manifest-info --in <path> [--depot-key <hex>]\n"
              << "  cauth steam depot file-list --in <manifest> [--depot-key <hex>] [--filter <text>] [--limit <count>]\n"
              << "  cauth steam depot verify-local --in <manifest> --local-root <path> [--depot-key <hex>] [--filter <text>]\n"
              << "  cauth steam depot chunk-download --in <manifest> (--file-index <index>|--file <path>) --chunk-index <index> --out <path> [--depot-key <hex>] [--process] [--max-count <count>]\n"
              << "  cauth steam depot file-download --in <manifest> (--file-index <index>|--file <path>) --depot-key <hex> --out <path> [--max-count <count>]\n"
              << "  cauth steam depot all-files-download --in <manifest> --depot-key <hex> --out-dir <path> [--max-count <count>]\n"
              << "  cauth steam cloud list --app-id <id> [--access-token <token>] [--remote-root <path>] [--count <count>] [--start-index <index>] [--extended-details 0|1]\n"
              << "  cauth steam cloud verify --app-id <id> --local-root <path> [--access-token <token>] [--remote-root <path>] [--include-extra-local] [--extended-details 0|1]\n"
              << "  cauth steam cloud pull --app-id <id> --local-root <path> [--access-token <token>] [--remote-root <path>] [--dry-run] [--conflict-policy default|local-wins|remote-wins|newer-wins|fail]\n"
              << "  cauth steam cloud push --app-id <id> --local-root <path> [--access-token <token>] [--remote-root <path>] [--dry-run] [--delete-remote-orphans] [--conflict-policy default|local-wins|remote-wins|newer-wins|fail]\n";
}

int run_cli(int argc, char** argv, std::ostream& out, std::ostream& err) {
    if (argc <= 1) {
        print_cli_usage();
        return 0;
    }

    const std::string_view command = argv[1];
    for (const auto& entry : kTopLevelCommands) {
        if (entry.name == command) return entry.handler(argc, argv, out, err);
    }

    err << "unknown command: " << command << '\n';
    print_cli_usage();
    return 2;
}

} // namespace cauth::cli
