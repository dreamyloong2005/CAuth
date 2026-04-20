#include "cli/steam_cli.hpp"
#include "cli/cli_support.hpp"

#include "core/platform/session_repository_factory.hpp"
#include "steam/auth/steam_auth_application.hpp"
#include "steam/auth/steam_auth_cm_application.hpp"
#include "steam/auth/steam_login_service.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {
using namespace cauth::cli::support;

enum class AuthCommandKind {
    Login,
    Status,
    WhoAmI,
    Accounts,
    UseAccount,
    RefreshAccess,
    WebCookies,
    TokenInfo,
    Clear,
    ClearAccount,
    ClearAll,
    Cm,
};

enum class AuthCmCommandKind {
    FrameTest,
    Servers,
    Probe,
    Logon,
    AppInfo,
};

struct ParsedAuthLoginCommand {
    cauth::steam::auth::SteamLoginRequest request;
    cauth::steam::auth::SteamPlatformLoginOptions options;
};

struct ParsedAuthCmCommand {
    AuthCmCommandKind kind = AuthCmCommandKind::Servers;
    cauth::core::cm::CmServerQuery query;
    std::uint32_t app_id = 0;
    bool debug_app_info = false;
};

struct ParsedAuthCommand {
    AuthCommandKind kind = AuthCommandKind::Status;
    std::optional<ParsedAuthLoginCommand> login;
    std::optional<ParsedAuthCmCommand> cm;
    std::string steam_id;
};

struct ParsedAuthResult {
    bool ok = false;
    int exit_code = 2;
    ParsedAuthCommand command;
};

ParsedAuthResult parse_auth_cm_command(int argc, char** argv) {
    ParsedAuthResult result;
    if (argc < 3) {
        std::cerr << "missing steam auth cm command\n";
        cauth::cli::print_cli_usage();
        return result;
    }

    ParsedAuthCmCommand request;
    request.debug_app_info = env_flag_enabled("CAUTH_DEBUG_APPINFO");
    const std::string_view subcommand = argv[2];
    if (subcommand == "frame-test") {
        request.kind = AuthCmCommandKind::FrameTest;
    } else if (subcommand == "servers") {
        request.kind = AuthCmCommandKind::Servers;
    } else if (subcommand == "probe") {
        request.kind = AuthCmCommandKind::Probe;
    } else if (subcommand == "logon") {
        request.kind = AuthCmCommandKind::Logon;
    } else if (subcommand == "app-info") {
        request.kind = AuthCmCommandKind::AppInfo;
    } else {
        std::cerr << "unknown steam auth cm command: " << subcommand << '\n';
        cauth::cli::print_cli_usage();
        return result;
    }

    if (request.kind == AuthCmCommandKind::Probe ||
        request.kind == AuthCmCommandKind::Logon ||
        request.kind == AuthCmCommandKind::AppInfo) {
        request.query.protocol = cauth::core::cm::CmServerProtocol::WebSocket;
        request.query.max_count = 5;
    }

    for (int index = 3; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if ((request.kind == AuthCmCommandKind::Probe ||
             request.kind == AuthCmCommandKind::Logon ||
             request.kind == AuthCmCommandKind::AppInfo) &&
            arg == "--protocol") {
            std::cerr << "steam auth cm " << subcommand << " only supports websocket endpoints right now\n";
            return result;
        }
        if (arg == "--protocol" && index + 1 < argc) {
            const std::string_view protocol = argv[++index];
            if (protocol == "websocket") {
                request.query.protocol = cauth::core::cm::CmServerProtocol::WebSocket;
                continue;
            }
            if (protocol == "tcp") {
                request.query.protocol = cauth::core::cm::CmServerProtocol::Tcp;
                continue;
            }
            std::cerr << "unknown CM protocol: " << protocol << '\n';
            return result;
        }
        if (arg == "--max-count" && index + 1 < argc) {
            request.query.max_count = static_cast<std::uint32_t>(std::max(1, std::atoi(argv[++index])));
            continue;
        }
        if (request.kind == AuthCmCommandKind::AppInfo && arg == "--app-id" && index + 1 < argc) {
            request.app_id = static_cast<std::uint32_t>(std::max(0, std::atoi(argv[++index])));
            continue;
        }
        std::cerr << "unknown or incomplete steam auth cm option: " << arg << '\n';
        cauth::cli::print_cli_usage();
        return result;
    }

    if (request.kind == AuthCmCommandKind::AppInfo && request.app_id == 0) {
        std::cerr << "steam auth cm app-info requires --app-id <id>\n";
        return result;
    }

    result.ok = true;
    result.command.kind = AuthCommandKind::Cm;
    result.command.cm = request;
    return result;
}

ParsedAuthResult parse_auth_command(int argc, char** argv) {
    ParsedAuthResult result;
    if (argc < 3) {
        std::cerr << "missing steam auth command\n";
        cauth::cli::print_cli_usage();
        return result;
    }

    const std::string_view subcommand = argv[2];
    if (subcommand == "status") {
        result.ok = true;
        result.command.kind = AuthCommandKind::Status;
        return result;
    }
    if (subcommand == "whoami") {
        result.ok = true;
        result.command.kind = AuthCommandKind::WhoAmI;
        return result;
    }
    if (subcommand == "accounts") {
        result.ok = true;
        result.command.kind = AuthCommandKind::Accounts;
        return result;
    }
    if (subcommand == "use") {
        for (int index = 3; index < argc; ++index) {
            const std::string_view arg = argv[index];
            if (arg == "--steam-id" && index + 1 < argc) {
                result.command.steam_id = argv[++index];
                continue;
            }
            std::cerr << "unknown or incomplete steam auth use option: " << arg << '\n';
            cauth::cli::print_cli_usage();
            return result;
        }
        if (result.command.steam_id.empty()) {
            std::cerr << "steam auth use requires --steam-id <id>\n";
            return result;
        }
        result.ok = true;
        result.command.kind = AuthCommandKind::UseAccount;
        return result;
    }
    if (subcommand == "refresh-access") {
        result.ok = true;
        result.command.kind = AuthCommandKind::RefreshAccess;
        return result;
    }
    if (subcommand == "web-cookies") {
        result.ok = true;
        result.command.kind = AuthCommandKind::WebCookies;
        return result;
    }
    if (subcommand == "token-info") {
        result.ok = true;
        result.command.kind = AuthCommandKind::TokenInfo;
        return result;
    }
    if (subcommand == "clear") {
        for (int index = 3; index < argc; ++index) {
            const std::string_view arg = argv[index];
            if (arg == "--all") {
                result.command.kind = AuthCommandKind::ClearAll;
                continue;
            }
            if (arg == "--steam-id" && index + 1 < argc) {
                result.command.kind = AuthCommandKind::ClearAccount;
                result.command.steam_id = argv[++index];
                continue;
            }
            std::cerr << "unknown or incomplete steam auth clear option: " << arg << '\n';
            cauth::cli::print_cli_usage();
            return result;
        }
        if (result.command.kind == AuthCommandKind::ClearAccount &&
            result.command.steam_id.empty()) {
            std::cerr << "steam auth clear --steam-id requires <id>\n";
            return result;
        }
        result.ok = true;
        if (result.command.kind == AuthCommandKind::Status) {
            result.command.kind = AuthCommandKind::Clear;
        }
        return result;
    }
    if (subcommand == "cm") return parse_auth_cm_command(argc - 1, argv + 1);

    if (subcommand != "login" && subcommand != "login-web" && subcommand != "login-mobile") {
        std::cerr << "unknown steam auth command: " << subcommand << '\n';
        cauth::cli::print_cli_usage();
        return result;
    }

    ParsedAuthLoginCommand request;
    const bool use_web_auth = subcommand == "login-web";
    const bool use_mobile_auth = subcommand == "login-mobile";
    request.options.cm_max_count = 5;

    for (int index = 3; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--username" && index + 1 < argc) {
            request.request.account_name = argv[++index];
            continue;
        }
        if (arg == "--guard-code" && index + 1 < argc) {
            request.request.steam_guard_code = argv[++index];
            continue;
        }
        if (arg == "--device-name" && index + 1 < argc) {
            request.request.device_name = argv[++index];
            continue;
        }
        if (arg == "--platform") {
            std::cerr << "--platform is no longer supported; auth login uses steam-client and auth login-web uses web-browser\n";
            return result;
        }
        if (arg == "--poll-attempts" && index + 1 < argc) {
            request.options.authenticator_options.max_poll_attempts = std::max(1, std::atoi(argv[++index]));
            continue;
        }
        if (arg == "--cm-max-count" && index + 1 < argc) {
            if (use_web_auth || use_mobile_auth) {
                std::cerr << "--cm-max-count is only valid for CM auth login\n";
                return result;
            }
            request.options.cm_max_count = static_cast<std::uint32_t>(std::max(1, std::atoi(argv[++index])));
            continue;
        }
        if (arg == "--password-stdin") {
            std::getline(std::cin, request.request.password);
            continue;
        }
        std::cerr << "unknown or incomplete steam auth login option: " << arg << '\n';
        cauth::cli::print_cli_usage();
        return result;
    }

    request.options.authenticator_options.on_poll_waiting =
        [](int attempt, int max_attempts, double interval_seconds) {
            std::cerr << "Waiting for Steam confirmation... poll " << attempt << '/'
                      << max_attempts << ", next check in " << interval_seconds << "s\n";
        };

    request.request.platform_type =
        use_web_auth ? cauth::steam::auth::SteamLoginPlatformType::WebBrowser
        : use_mobile_auth ? cauth::steam::auth::SteamLoginPlatformType::MobileApp
                          : cauth::steam::auth::SteamLoginPlatformType::SteamClient;

    result.ok = true;
    result.command.kind = AuthCommandKind::Login;
    result.command.login = std::move(request);
    return result;
}

int run_auth_cm_command(const ParsedAuthCmCommand& request) {
    switch (request.kind) {
    case AuthCmCommandKind::FrameTest:
        return cauth::steam::auth::run_cm_frame_test(std::cout, std::cerr);
    case AuthCmCommandKind::Servers:
        return cauth::steam::auth::run_cm_servers(request.query, std::cout, std::cerr);
    case AuthCmCommandKind::Probe:
        return cauth::steam::auth::run_cm_probe(request.query, std::cout, std::cerr);
    case AuthCmCommandKind::Logon:
        return cauth::steam::auth::run_cm_logon(request.query, std::cout, std::cerr);
    case AuthCmCommandKind::AppInfo:
        return cauth::steam::auth::run_cm_app_info(
            request.query,
            request.app_id,
            request.debug_app_info,
            std::cout,
            std::cerr);
    }
    return 2;
}

} // namespace

namespace cauth::cli {

int run_steam_auth(int argc, char** argv) {
    const auto parsed = parse_auth_command(argc, argv);
    if (!parsed.ok) return parsed.exit_code;

    const auto store = cauth::core::platform::make_platform_session_repository();
    switch (parsed.command.kind) {
    case AuthCommandKind::Login:
        return cauth::steam::auth::run_login(
            *store,
            parsed.command.login->request,
            parsed.command.login->options,
            std::cout,
            std::cerr);
    case AuthCommandKind::Status:
        return cauth::steam::auth::print_status(*store, std::cout);
    case AuthCommandKind::WhoAmI:
        return cauth::steam::auth::print_whoami(*store, std::cout, std::cerr);
    case AuthCommandKind::Accounts:
        return cauth::steam::auth::print_saved_accounts(*store, std::cout);
    case AuthCommandKind::UseAccount:
        return cauth::steam::auth::use_saved_account(
            *store,
            parsed.command.steam_id,
            std::cout,
            std::cerr);
    case AuthCommandKind::RefreshAccess:
        return cauth::steam::auth::refresh_saved_access_token_from_store(*store, std::cout, std::cerr);
    case AuthCommandKind::WebCookies:
        return cauth::steam::auth::print_saved_web_cookies(*store, std::cout, std::cerr);
    case AuthCommandKind::TokenInfo:
        return cauth::steam::auth::print_token_info(*store, std::cout, std::cerr);
    case AuthCommandKind::Clear:
        return cauth::steam::auth::clear_saved_session(*store, std::cout);
    case AuthCommandKind::ClearAccount:
        return cauth::steam::auth::clear_saved_account(
            *store,
            parsed.command.steam_id,
            std::cout);
    case AuthCommandKind::ClearAll:
        return cauth::steam::auth::clear_all_saved_accounts(*store, std::cout);
    case AuthCommandKind::Cm:
        return run_auth_cm_command(*parsed.command.cm);
    }
    return 2;
}

} // namespace cauth::cli
