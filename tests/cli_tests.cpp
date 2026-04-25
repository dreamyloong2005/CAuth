#include "cli/steam_cli.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

class ScopedStreamRedirect {
  public:
    ScopedStreamRedirect(std::ostream& stream, std::streambuf* replacement)
        : stream_(stream), original_(stream.rdbuf(replacement)) {}

    ~ScopedStreamRedirect() { stream_.rdbuf(original_); }

    ScopedStreamRedirect(const ScopedStreamRedirect&) = delete;
    ScopedStreamRedirect& operator=(const ScopedStreamRedirect&) = delete;

  private:
    std::ostream& stream_;
    std::streambuf* original_;
};

std::vector<char*> make_argv(std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args) argv.push_back(arg.data());
    return argv;
}

bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

int expect_true(bool condition, const char* message) {
    if (condition) return 0;
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main() {
    {
        std::ostringstream out;
        std::ostringstream err;
        std::vector<std::string> args = {"cauth", "--version"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_cli(static_cast<int>(argv.size()), argv.data(), out, err);
        if (expect_true(exit_code == 0, "version command should succeed") != 0) return 1;
        if (expect_true(contains(out.str(), "CAuth 0.6.1"), "version output should include semantic version") != 0) return 1;
        if (expect_true(err.str().empty(), "version command should not write stderr") != 0) return 1;
    }

    {
        std::ostringstream out;
        std::ostringstream err;
        std::vector<std::string> args = {"cauth", "--memory-session-store", "--version"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_cli(static_cast<int>(argv.size()), argv.data(), out, err);
        if (expect_true(exit_code == 0, "version command with memory session store should succeed") != 0) return 1;
        if (expect_true(contains(out.str(), "CAuth 0.6.1"), "memory session store should still dispatch version") != 0) return 1;
    }

    {
        std::ostringstream out;
        std::ostringstream err;
        std::vector<std::string> args = {"cauth", "doctor"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_cli(static_cast<int>(argv.size()), argv.data(), out, err);
        if (expect_true(exit_code == 0, "doctor command should succeed") != 0) return 1;
        if (expect_true(contains(out.str(), "CAuth core: ok"), "doctor output should report core ok") != 0) return 1;
        if (expect_true(err.str().empty(), "doctor command should not write stderr") != 0) return 1;
    }

    {
        std::ostringstream out;
        std::ostringstream err;
        std::vector<std::string> args = {"cauth", "unknown"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_cli(static_cast<int>(argv.size()), argv.data(), out, err);
        if (expect_true(exit_code == 2, "unknown top-level command should return usage error") != 0) return 1;
        if (expect_true(contains(err.str(), "unknown command: unknown"), "unknown top-level command should explain failure") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {"cauth", "steam"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_cli(static_cast<int>(argv.size()), argv.data(), std::cout, std::cerr);
        if (expect_true(exit_code == 2, "missing steam subcommand should return usage error") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "missing steam command"), "missing steam subcommand should report error") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {"cauth", "steam", "auth", "wat"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_auth(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "unknown steam auth command should return usage error") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "unknown steam auth command: wat"), "unknown steam auth command should explain failure") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {"cauth", "steam", "auth", "login-web", "--cm-max-count", "10"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_auth(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "login-web with cm-max-count should be rejected") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "--cm-max-count is only valid for CM auth login"), "login-web should reject cm-max-count") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "auth", "login-web", "--route-endpoint", "cmp1-hkg1.steamserver.net:27022"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_auth(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "login-web with route-endpoint should be rejected") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "--route-endpoint is only valid for CM auth login"),
                        "login-web should reject route-endpoint") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {"cauth", "steam", "auth", "cm", "app-info"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_auth(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "cm app-info without app-id should be rejected") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "steam auth cm app-info requires --app-id <id>"), "cm app-info should require app-id") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "auth", "cm", "routes", "--protocol", "tcp"};
        auto argv = make_argv(args);
        const auto exit_code =
            cauth::cli::run_steam_auth(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "cm routes should reject tcp protocol") != 0) return 1;
        if (expect_true(
                contains(captured_err.str(), "steam auth cm routes only supports websocket endpoints right now"),
                "cm routes should explain websocket-only support") != 0) {
            return 1;
        }
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {"cauth", "steam", "depot", "wat"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_depot(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "unknown steam depot command should return usage error") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "unknown steam depot command: wat"), "unknown steam depot command should explain failure") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "depot", "key", "--steam-id", "76561198000000000", "--app-id", "440"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_depot(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "depot key without depot-id should be rejected") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "steam depot key requires --depot-id <id>"), "depot key should require depot-id") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {"cauth", "steam", "depot", "verify-local", "--in", "manifest.bin"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_depot(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "verify-local without local-root should be rejected") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "steam depot verify-local requires --local-root <path>"), "verify-local should require local-root") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {"cauth", "steam", "depot", "chunk-download", "--in", "manifest.bin", "--file-index", "0", "--out", "chunk.bin"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_depot(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "chunk-download without chunk-index should be rejected") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "steam depot chunk-download requires (--file-index <index>|--file <path>) --chunk-index <index> --out <path>"), "chunk-download should require chunk-index") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "depot", "manifest-download",
            "--depot-id", "441",
            "--manifest-gid", "123",
            "--request-code", "456",
            "--out", "manifest.bin",
            "--skip-existing",
            "--fail-if-exists"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_depot(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "conflicting depot write mode flags should be rejected") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "only one of --overwrite, --skip-existing, or --fail-if-exists may be used"),
                        "depot parser should reject conflicting write mode flags") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {"cauth", "steam", "cloud", "wat"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_cloud(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "unknown steam cloud command should return usage error") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "unknown steam cloud command: wat"), "unknown steam cloud command should explain failure") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "cloud", "verify", "--steam-id", "76561198000000000", "--app-id", "440"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_cloud(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "steam cloud verify without local-root should be rejected") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "steam cloud verify requires --local-root <path>"), "steam cloud verify should require local-root") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "cloud", "pull", "--steam-id", "76561198000000000", "--app-id", "440"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_cloud(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "steam cloud pull without local-root should be rejected") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "steam cloud pull requires --local-root <path>"), "steam cloud pull should require local-root") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "cloud", "push",
            "--steam-id", "76561198000000000",
            "--app-id", "440",
            "--local-root", "D:/saves",
            "--atomic-write"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_cloud(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "steam cloud push should reject local write flags") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "steam cloud push does not support local write options"),
                        "steam cloud push should explain unsupported local write flags") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {"cauth", "steam", "cloud", "push", "--app-id", "440", "--local-root", "D:/saves", "--conflict-policy", "bogus"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_cloud(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "steam cloud push with invalid conflict policy should be rejected") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "unknown steam cloud conflict policy"), "steam cloud should validate conflict policy") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "cloud", "pull",
            "--steam-id", "76561198000000000",
            "--app-id", "440",
            "--local-root", "D:/saves",
            "--backend", "bogus"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_steam_cloud(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "steam cloud should reject invalid backend values") != 0) return 1;
        if (expect_true(contains(captured_err.str(), "unknown steam cloud backend"),
                        "steam cloud should validate backend values") != 0) return 1;
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "cloud", "list",
            "--steam-id", "76561198000000000",
            "--app-id", "440",
            "--backend", "web",
            "--access-token", "token"};
        auto argv = make_argv(args);
        const auto exit_code =
            cauth::cli::run_steam_cloud(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 1, "steam cloud web backend should fail fast") != 0) {
            return 1;
        }
        if (expect_true(
                contains(captured_err.str(), "Steam Cloud web backend is currently unsupported"),
                "steam cloud web backend should explain the unsupported state") != 0) {
            return 1;
        }
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "cloud", "routes",
            "--steam-id", "76561198000000000",
            "--app-id", "440",
            "--backend", "web"};
        auto argv = make_argv(args);
        const auto exit_code =
            cauth::cli::run_steam_cloud(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 1, "steam cloud web routes should fail fast") != 0) {
            return 1;
        }
        if (expect_true(
                contains(captured_err.str(), "Steam Cloud web backend is currently unsupported"),
                "steam cloud web routes should explain the unsupported state") != 0) {
            return 1;
        }
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "--memory-session-store", "steam", "cloud", "web-page-list",
            "--steam-id", "76561198000000000",
            "--app-id", "440"};
        auto argv = make_argv(args);
        const auto exit_code =
            cauth::cli::run_cli(static_cast<int>(argv.size()), argv.data(), std::cout, std::cerr);
        if (expect_true(exit_code == 1,
                        "steam cloud web-page-list should return structured failure without web material") != 0) {
            return 1;
        }
        if (expect_true(
                contains(captured_err.str(), "web cookie auth materialization failed"),
                "steam cloud web-page-list should surface diagnostic store-page auth failure") != 0) {
            return 1;
        }
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {
            "cauth", "steam", "cloud", "routes",
            "--steam-id", "76561198000000000",
            "--app-id", "440",
            "--task", "bogus"};
        auto argv = make_argv(args);
        const auto exit_code =
            cauth::cli::run_steam_cloud(static_cast<int>(argv.size()) - 1, argv.data() + 1);
        if (expect_true(exit_code == 2, "steam cloud routes should reject unknown route tasks") != 0) {
            return 1;
        }
        if (expect_true(contains(captured_err.str(), "unknown steam cloud route task"),
                        "steam cloud routes should validate route task values") != 0) {
            return 1;
        }
    }

    {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        ScopedStreamRedirect redirect_out(std::cout, captured_out.rdbuf());
        ScopedStreamRedirect redirect_err(std::cerr, captured_err.rdbuf());
        std::vector<std::string> args = {"cauth", "steam", "cloud", "wat"};
        auto argv = make_argv(args);
        const auto exit_code = cauth::cli::run_cli(static_cast<int>(argv.size()), argv.data(), std::cout, std::cerr);
        if (expect_true(exit_code == 2, "top-level steam cloud dispatch should still report a usage error for bad cloud subcommands") != 0) {
            return 1;
        }
        if (expect_true(contains(captured_err.str(), "unknown steam cloud command: wat"),
                        "top-level steam cloud dispatch should reach the cloud handler") != 0) {
            return 1;
        }
    }

    return 0;
}
