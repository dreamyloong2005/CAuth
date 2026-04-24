#include "cauth/steam_cloud.hpp"
#include "core/hash/sha1.hpp"
#include "core/runtime/session/memory_session_repository.hpp"
#include "steam/auth/steam_session_identity.hpp"
#include "steam/cloud/steam_cloud_test_hooks.hpp"
#include "steam/cloud/steam_cloud_upload_service.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

void reset_mock_sequences();

struct ScopedCloudHooksReset {
    ~ScopedCloudHooksReset() {
        cauth::steam::cloud::testing::clear_cloud_test_hooks();
        reset_mock_sequences();
    }
};

cauth::steam::cloud::SteamCloudFileListResult g_mock_list_result;
cauth::steam::cloud::SteamCloudDownloadResult g_mock_download_response;
cauth::steam::cloud::SteamCloudUploadResult g_mock_upload_result;
std::vector<cauth::steam::cloud::SteamCloudFileListResult> g_mock_list_sequence;
std::vector<cauth::steam::cloud::SteamCloudDownloadResult> g_mock_download_sequence;
std::vector<cauth::steam::cloud::SteamCloudUploadFile> g_captured_upload_files;
std::vector<std::string> g_captured_delete_files;
cauth::steam::cloud::SteamCloudRequest g_last_list_request;
int g_list_call_count = 0;
int g_download_call_count = 0;
int g_upload_call_count = 0;

void reset_mock_sequences() {
    g_mock_list_sequence.clear();
    g_mock_download_sequence.clear();
    g_list_call_count = 0;
    g_download_call_count = 0;
}

cauth::steam::cloud::SteamCloudFileListResult mock_list_remote_files(
    const cauth::steam::cloud::SteamCloudRequest& request,
    std::uint32_t,
    std::uint32_t,
    bool) {
    ++g_list_call_count;
    g_last_list_request = request;
    if (!g_mock_list_sequence.empty()) {
        const auto sequence_index =
            static_cast<std::size_t>(std::min<int>(g_list_call_count - 1,
                                                   static_cast<int>(g_mock_list_sequence.size() - 1)));
        return g_mock_list_sequence[sequence_index];
    }
    return g_mock_list_result;
}

cauth::steam::cloud::SteamCloudDownloadResult mock_download_file(
    const cauth::steam::cloud::SteamCloudRequest&,
    const cauth::steam::cloud::SteamCloudFileEntry&) {
    ++g_download_call_count;
    if (!g_mock_download_sequence.empty()) {
        const auto sequence_index =
            static_cast<std::size_t>(std::min<int>(g_download_call_count - 1,
                                                   static_cast<int>(g_mock_download_sequence.size() - 1)));
        return g_mock_download_sequence[sequence_index];
    }
    return g_mock_download_response;
}

cauth::steam::cloud::SteamCloudUploadResult mock_upload_cloud_files(
    const cauth::steam::cloud::SteamCloudWebAuthContext&,
    std::uint32_t,
    std::string_view,
    const std::vector<cauth::steam::cloud::SteamCloudUploadFile>& files,
    const std::vector<std::string>& files_to_delete) {
    ++g_upload_call_count;
    g_captured_upload_files = files;
    g_captured_delete_files = files_to_delete;
    return g_mock_upload_result;
}

std::filesystem::path make_temp_dir(const char* name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
    return path;
}

bool write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    cauth::steam::cloud::SteamCloudRequest request;
    request.app_id = 440;
    request.access_token = "token";
    request.session_type = std::string{cauth::steam::auth::kSteamSessionTypeSteamClient};
    request.local_root = "D:/saves";
    request.remote_root = "remote";
    request.dry_run = true;
    request.delete_remote_orphans = true;
    request.conflict_policy = cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins;

    const auto url =
        cauth::steam::cloud::build_enumerate_user_files_url(request, 10, 5, true);
    if (url.find("ICloudService/EnumerateUserFiles/v1/") == std::string::npos ||
        url.find("access_token=token") == std::string::npos ||
        url.find("appid=440") == std::string::npos ||
        url.find("count=10") == std::string::npos ||
        url.find("start_index=5") == std::string::npos) {
        std::cerr << "steam cloud enumerate URL should include request parameters\n";
        return 1;
    }

    cauth::steam::cloud::SteamCloudRequest cookie_request = request;
    cookie_request.access_token.clear();
    cookie_request.web_cookie_header = "steamLoginSecure=cookie";
    const auto cookie_url =
        cauth::steam::cloud::build_enumerate_user_files_url(cookie_request, 10, 5, true);
    if (cookie_url.find("https://steamcommunity.com/ICloudService/EnumerateUserFiles/v1/") != 0 ||
        cookie_url.find("access_token=") != std::string::npos) {
        std::cerr << "steam cloud enumerate URL should switch to cookie session mode without access_token\n";
        return 1;
    }

    cauth::steam::cloud::SteamCloudRequest token_preferred_request = cookie_request;
    token_preferred_request.access_token = "store-web-token";
    const auto token_preferred_url =
        cauth::steam::cloud::build_enumerate_user_files_url(token_preferred_request, 10, 5, true);
    if (token_preferred_url.find("https://api.steampowered.com/ICloudService/EnumerateUserFiles/v1/") != 0 ||
        token_preferred_url.find("access_token=store-web-token") == std::string::npos) {
        std::cerr << "steam cloud enumerate URL should prefer API token mode when both token and cookies exist\n";
        return 1;
    }

    const auto parsed = cauth::steam::cloud::parse_enumerate_user_files_response(
        440,
        R"({"response":{"total_files":2,"files":[{"appid":440,"ugcid":"123","filename":"save1.sav","timestamp":"111","file_size":64,"url":"https://cdn.example/save1","steamid_creator":"7656119","flags":0,"platforms_to_sync":"windows,android","file_sha":"abc"},{"appid":440,"ugcid":"456","filename":"save2.sav","timestamp":"222","file_size":128,"url":"https://cdn.example/save2","steamid_creator":"7656120","flags":4,"platforms_to_sync":"android","file_sha":"def"}]}})",
        1);
    if (!parsed.ok || parsed.total_files != 2 || parsed.files.size() != 2) {
        std::cerr << "steam cloud enumerate parser should expose files\n";
        return 1;
    }
    if (parsed.files[0].filename != "save1.sav" || parsed.files[0].ugc_id != 123 ||
        parsed.files[0].platforms_to_sync != "windows,android") {
        std::cerr << "steam cloud enumerate parser should preserve file metadata\n";
        return 1;
    }

    cauth::steam::cloud::SteamCloudRequest invalid_pull_request;
    invalid_pull_request.app_id = 440;
    invalid_pull_request.access_token = "token";
    invalid_pull_request.session_type =
        std::string{cauth::steam::auth::kSteamSessionTypeSteamClient};
    const auto invalid_pull = cauth::steam::cloud::pull_cloud_save(invalid_pull_request);
    if (invalid_pull.ok || invalid_pull.message.find("local_root is required") == std::string::npos) {
        std::cerr << "steam cloud pull should validate local_root before download\n";
        return 1;
    }

    {
        cauth::steam::cloud::SteamCloudRequest diagnostic_request;
        diagnostic_request.app_id = 440;
        const auto diagnostic_result =
            cauth::steam::cloud::fetch_remote_file_list_via_web_page_diagnostic(
                diagnostic_request);
        if (diagnostic_result.ok ||
            diagnostic_result.message.find("web cookie auth materialization failed") ==
                std::string::npos) {
            std::cerr << "steam cloud web-page diagnostic list should fail cleanly without web auth material\n";
            return 1;
        }
    }

    const auto batch_form = cauth::steam::cloud::build_begin_app_upload_batch_form_body(
        "token", 440, "CAuth", {"remote/save1.sav", "remote/save2.sav"}, {"remote/old.sav"});
    if (batch_form.find("access_token=token") == std::string::npos ||
        batch_form.find("BeginAppUploadBatch") != std::string::npos ||
        batch_form.find("input_json=") == std::string::npos ||
        batch_form.find("remote%2Fsave1.sav") == std::string::npos ||
        batch_form.find("remote%2Fold.sav") == std::string::npos) {
        std::cerr << "steam cloud batch form body should encode token and upload list\n";
        return 1;
    }

    const auto cookie_batch_form = cauth::steam::cloud::build_begin_app_upload_batch_form_body(
        cauth::steam::cloud::SteamCloudWebAuthContext{{}, "steamLoginSecure=cookie", {}, {}},
        440,
        "CAuth",
        {"remote/save1.sav"},
        {});
    if (cookie_batch_form.find("access_token=") != std::string::npos ||
        cookie_batch_form.find("input_json=") == std::string::npos ||
        cookie_batch_form.find("remote%2Fsave1.sav") == std::string::npos) {
        std::cerr << "steam cloud batch form body should support cookie-backed uploads without access_token\n";
        return 1;
    }

    cauth::steam::cloud::SteamCloudResult pull_result;
    cauth::steam::cloud::SteamCloudResult push_result;
    {
        ScopedCloudHooksReset hooks_reset;
        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.eresult = 1;
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);
        pull_result = cauth::steam::cloud::pull_cloud_save(request);
        push_result = cauth::steam::cloud::push_cloud_save(request);
    }

    if (pull_result.app_id != 440 || pull_result.direction != cauth::steam::cloud::SteamCloudDirection::Pull) {
        std::cerr << "steam cloud pull should preserve request metadata\n";
        return 1;
    }
    if (push_result.app_id != 440 || push_result.direction != cauth::steam::cloud::SteamCloudDirection::Push) {
        std::cerr << "steam cloud push should preserve request metadata\n";
        return 1;
    }
    if (pull_result.message.empty() || push_result.message.empty()) {
        std::cerr << "steam cloud operations should expose a message\n";
        return 1;
    }
    if (pull_result.conflict_policy != cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins ||
        push_result.conflict_policy != cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins) {
        std::cerr << "steam cloud results should preserve conflict policy\n";
        return 1;
    }
    if (pull_result.deleted_count != 0 || push_result.deleted_count != 0) {
        std::cerr << "dry-run placeholder cloud results should keep delete counts stable\n";
        return 1;
    }

    {
        ScopedCloudHooksReset hooks_reset;
        cauth::core::runtime::MemorySessionRepository store;
        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 3;
        g_mock_list_result.eresult = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{440, 1, "saves/a.sav", 10, 1, {}, 0, 0, "all", "a"},
            cauth::steam::cloud::SteamCloudFileEntry{440, 2, "saves/b.sav", 20, 1, {}, 0, 0, "all", "b"},
            cauth::steam::cloud::SteamCloudFileEntry{440, 3, "other/c.sav", 30, 1, {}, 0, 0, "all", "c"},
        };
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);

        std::ostringstream out;
        std::ostringstream err;
        cauth::steam::cloud::SteamCloudRequest list_request;
        list_request.app_id = 440;
        list_request.steam_id = 76561198000000000ULL;
        list_request.access_token = "token";
        list_request.remote_root = "saves";
        if (cauth::steam::cloud::print_remote_files(store, list_request, 10, 0, true, out, err) != 0) {
            std::cerr << "print_remote_files should succeed with mocked list data\n";
            return 1;
        }
        if (out.str().find("matched=2") == std::string::npos ||
            out.str().find("filtered_out=1") == std::string::npos ||
            out.str().find("file=saves/a.sav") == std::string::npos ||
            out.str().find("file=other/c.sav") != std::string::npos) {
            std::cerr << "print_remote_files should report filtered stats and only print matching files\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-pull-newer");
        const auto local_path = temp_dir / "save1.sav";
        if (!write_text_file(local_path, "local")) {
            std::cerr << "failed to prepare local pull fixture\n";
            return 1;
        }

        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 9999999999ULL, 6, "mock://save1", 0, 0, "all", "deadbeef"}
        };
        g_mock_download_response = {};
        g_mock_download_response.ok = true;
        g_mock_download_response.bytes = {'r', 'e', 'm', 'o', 't', 'e'};
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);
        cauth::steam::cloud::testing::set_download_file_hook(&mock_download_file);

        cauth::steam::cloud::SteamCloudRequest pull_request;
        pull_request.app_id = 440;
        pull_request.access_token = "token";
        pull_request.local_root = temp_dir.string();
        pull_request.conflict_policy = cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins;
        const auto result = cauth::steam::cloud::pull_cloud_save(pull_request);
        if (!result.ok || result.transferred_count != 1 || result.conflict_count != 1 ||
            read_text_file(local_path) != "remote") {
            std::cerr << "pull should download and overwrite when remote is newer\n";
            return 1;
        }
    }

    {
        const auto temp_dir = make_temp_dir("cauth-cloud-pull-web-unsupported");
        cauth::steam::cloud::SteamCloudRequest pull_request;
        pull_request.app_id = 440;
        pull_request.access_token = "token";
        pull_request.session_type = std::string{cauth::steam::auth::kSteamSessionTypeWebBrowser};
        pull_request.backend = cauth::steam::cloud::SteamCloudBackend::WebApi;
        pull_request.local_root = temp_dir.string();
        pull_request.conflict_policy = cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins;
        const auto result = cauth::steam::cloud::pull_cloud_save(pull_request);
        if (result.ok ||
            result.message.find("Steam Cloud web backend is currently unsupported") ==
                std::string::npos) {
            std::cerr << "web pull should fail fast with an unsupported-backend message\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        cauth::core::runtime::MemorySessionRepository store;
        cauth::core::session::AuthSession saved;
        saved.provider = "steam";
        saved.subject_id = "76561198000000000";
        saved.refresh_token = "refresh-token";
        saved.access_token = "";
        store.save_auth_session(saved);

        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.eresult = 1;
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);

        std::ostringstream out;
        std::ostringstream err;
        cauth::steam::cloud::SteamCloudRequest list_request;
        list_request.app_id = 440;
        list_request.steam_id = 76561198000000000ULL;
        if (cauth::steam::cloud::print_remote_files(store, list_request, 10, 0, true, out, err) != 0) {
            std::cerr << "print_remote_files should fill saved Steam session fields\n";
            return 1;
        }
        if (g_last_list_request.refresh_token != "refresh-token" ||
            g_last_list_request.steam_id != 76561198000000000ULL) {
            std::cerr << "saved Steam session should flow into cloud requests\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        cauth::core::runtime::MemorySessionRepository store;

        cauth::core::session::AuthSession client_session;
        client_session.provider = "steam";
        client_session.subject_id = "76561198000000000";
        client_session.account_name = "test-account";
        client_session.refresh_token = "client-refresh";
        client_session.access_token = "client-access";
        client_session.session_type = std::string{cauth::steam::auth::kSteamSessionTypeSteamClient};
        client_session.created_at = std::chrono::system_clock::time_point{std::chrono::seconds{100}};
        store.save_auth_session(client_session);

        cauth::core::session::AuthSession web_session = client_session;
        web_session.refresh_token = "web-refresh";
        web_session.access_token = "web-access";
        web_session.session_type = std::string{cauth::steam::auth::kSteamSessionTypeWebBrowser};
        web_session.created_at = std::chrono::system_clock::time_point{std::chrono::seconds{200}};
        store.save_auth_session(web_session);

        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.eresult = 1;
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);

        std::ostringstream out;
        std::ostringstream err;
        cauth::steam::cloud::SteamCloudRequest list_request;
        list_request.app_id = 440;
        list_request.steam_id = 76561198000000000ULL;
        if (cauth::steam::cloud::print_remote_files(store, list_request, 10, 0, true, out, err) != 0) {
            std::cerr << "cloud auto backend should succeed when client and web sessions coexist\n";
            return 1;
        }
        if (g_last_list_request.refresh_token != "client-refresh" ||
            g_last_list_request.access_token != "client-access" ||
            g_last_list_request.session_type != cauth::steam::auth::kSteamSessionTypeSteamClient) {
            std::cerr << "cloud auto backend should prefer steam-client session when available\n";
            return 1;
        }

        cauth::steam::cloud::testing::clear_cloud_test_hooks();
        out.str({});
        out.clear();
        err.str({});
        err.clear();
        list_request.backend = cauth::steam::cloud::SteamCloudBackend::WebApi;
        if (cauth::steam::cloud::print_remote_files(store, list_request, 10, 0, true, out, err) == 0) {
            std::cerr << "cloud web backend should fail fast when client and web sessions coexist\n";
            return 1;
        }
        if (err.str().find("Steam Cloud web backend is currently unsupported") ==
            std::string::npos) {
            std::cerr << "cloud web backend should explain the unsupported state\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-verify-basic");
        if (!write_text_file(temp_dir / "save1.sav", "same")) {
            std::cerr << "failed to prepare cloud verify fixture\n";
            return 1;
        }
        if (!write_text_file(temp_dir / "save2.sav", "different")) {
            std::cerr << "failed to prepare cloud verify mismatch fixture\n";
            return 1;
        }

        const auto same_sha = cauth::core::hash::sha1_to_hex(
            cauth::core::hash::sha1_digest(std::vector<std::uint8_t>{'s', 'a', 'm', 'e'}));
        const auto remote_sha = cauth::core::hash::sha1_to_hex(
            cauth::core::hash::sha1_digest(std::vector<std::uint8_t>{'r', 'e', 'm', 'o', 't', 'e'}));

        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 3;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 1, 4, {}, 0, 0, "all", same_sha},
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 2, "save2.sav", 2, 6, {}, 0, 0, "all", remote_sha},
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 3, "missing.sav", 3, 5, {}, 0, 0, "all", remote_sha},
        };
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);

        cauth::steam::cloud::SteamCloudRequest verify_request;
        verify_request.app_id = 440;
        verify_request.access_token = "token";
        verify_request.local_root = temp_dir.string();
        const auto result = cauth::steam::cloud::verify_cloud_local_files(verify_request);
        if (!result.ok || result.clean() || result.checked_count != 3 || result.ok_count != 1 ||
            result.mismatched_count != 1 || result.missing_count != 1 || result.total_count != 3 ||
            result.entries.size() != 3 || result.entries[0].remote_filename.empty()) {
            std::cerr << "cloud verify should classify ok, mismatch, and missing files\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-verify-size-only");
        if (!write_text_file(temp_dir / "save1.sav", "same")) {
            std::cerr << "failed to prepare cloud size-only fixture\n";
            return 1;
        }

        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 1, 4, {}, 0, 0, "all", {}},
        };
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);

        cauth::steam::cloud::SteamCloudRequest verify_request;
        verify_request.app_id = 440;
        verify_request.access_token = "token";
        verify_request.local_root = temp_dir.string();
        const auto result = cauth::steam::cloud::verify_cloud_local_files(verify_request);
        if (!result.ok || !result.clean() || result.size_only_count != 1 || result.ok_count != 1 ||
            result.entries.size() != 1 ||
            result.entries[0].status != cauth::steam::cloud::SteamCloudVerifyStatus::SizeOnly) {
            std::cerr << "cloud verify should fall back to size-only when remote sha is absent\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-verify-extra-local");
        if (!write_text_file(temp_dir / "existing.sav", "same")) {
            std::cerr << "failed to prepare cloud extra-local fixture\n";
            return 1;
        }
        if (!write_text_file(temp_dir / "extra.sav", "extra")) {
            std::cerr << "failed to prepare cloud extra-local file\n";
            return 1;
        }

        const auto same_sha = cauth::core::hash::sha1_to_hex(
            cauth::core::hash::sha1_digest(std::vector<std::uint8_t>{'s', 'a', 'm', 'e'}));
        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "existing.sav", 1, 4, {}, 0, 0, "all", same_sha},
        };
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);

        cauth::steam::cloud::SteamCloudRequest verify_request;
        verify_request.app_id = 440;
        verify_request.access_token = "token";
        verify_request.local_root = temp_dir.string();
        const auto result = cauth::steam::cloud::verify_cloud_local_files(verify_request, true);
        if (!result.ok || result.extra_local_count != 1 || !result.clean() ||
            result.entries.size() != 2 ||
            result.entries[1].status != cauth::steam::cloud::SteamCloudVerifyStatus::ExtraLocal) {
            std::cerr << "cloud verify should optionally report extra local files without failing clean state\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-pull-fail");
        const auto local_path = temp_dir / "save1.sav";
        if (!write_text_file(local_path, "local")) {
            std::cerr << "failed to prepare pull conflict fixture\n";
            return 1;
        }

        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 9999999999ULL, 6, "mock://save1", 0, 0, "all", "deadbeef"}
        };
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);

        cauth::steam::cloud::SteamCloudRequest pull_request;
        pull_request.app_id = 440;
        pull_request.access_token = "token";
        pull_request.local_root = temp_dir.string();
        pull_request.dry_run = true;
        pull_request.conflict_policy = cauth::steam::cloud::SteamCloudConflictPolicy::FailOnConflict;
        const auto result = cauth::steam::cloud::pull_cloud_save(pull_request);
        if (result.ok || result.conflict_count != 1 ||
            result.message.find("conflict detected") == std::string::npos) {
            std::cerr << "pull should fail immediately on conflict when policy is fail\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-push-upload");
        const auto local_path = temp_dir / "save1.sav";
        if (!write_text_file(local_path, "local-new")) {
            std::cerr << "failed to prepare push fixture\n";
            return 1;
        }

        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 2;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 1, 5, {}, 0, 0, "all", "older-sha"},
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 2, "stale.sav", 1, 5, {}, 0, 0, "all", "stale-sha"},
        };
        g_mock_upload_result = {true, "ok", true, true, 64};
        g_captured_upload_files.clear();
        g_captured_delete_files.clear();
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);
        cauth::steam::cloud::testing::set_upload_cloud_files_hook(&mock_upload_cloud_files);

        cauth::steam::cloud::SteamCloudRequest push_request;
        push_request.app_id = 440;
        push_request.access_token = "token";
        push_request.local_root = temp_dir.string();
        push_request.delete_remote_orphans = true;
        push_request.conflict_policy = cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins;
        const auto result = cauth::steam::cloud::push_cloud_save(push_request);
        if (!result.ok || result.transferred_count != 1 || result.deleted_count != 1 ||
            result.conflict_count != 1 || g_captured_upload_files.size() != 1 ||
            g_captured_delete_files.size() != 1 || g_captured_delete_files[0] != "stale.sav") {
            std::cerr << "push should upload changed files and delete remote orphans\n";
            return 1;
        }
        if (!result.resumable || !result.resumed || result.resume_from_bytes != 64) {
            std::cerr << "push should preserve resumable upload metadata\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-push-remote-wins");
        const auto local_path = temp_dir / "save1.sav";
        if (!write_text_file(local_path, "local-old")) {
            std::cerr << "failed to prepare remote-wins fixture\n";
            return 1;
        }

        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 9999999999ULL, 5, {}, 0, 0, "all", "remote-sha"},
        };
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);

        cauth::steam::cloud::SteamCloudRequest push_request;
        push_request.app_id = 440;
        push_request.access_token = "token";
        push_request.local_root = temp_dir.string();
        push_request.dry_run = true;
        push_request.conflict_policy = cauth::steam::cloud::SteamCloudConflictPolicy::RemoteWins;
        const auto result = cauth::steam::cloud::push_cloud_save(push_request);
        if (!result.ok || result.transferred_count != 0 || result.skipped_count != 1 ||
            result.conflict_count != 1) {
            std::cerr << "push should skip upload when remote wins on conflict\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-pull-dry-run");
        const auto local_path = temp_dir / "save1.sav";
        if (!write_text_file(local_path, "local")) {
            std::cerr << "failed to prepare pull dry-run fixture\n";
            return 1;
        }

        g_download_call_count = 0;
        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 2;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 9999999999ULL, 6, "mock://save1", 0, 0, "all", "deadbeef"},
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 2, "save2.sav", 10, 6, "mock://save2", 0, 0, "all", "beadfeed"},
        };
        g_mock_download_response = {};
        g_mock_download_response.ok = true;
        g_mock_download_response.bytes = {'r', 'e', 'm', 'o', 't', 'e'};
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);
        cauth::steam::cloud::testing::set_download_file_hook(&mock_download_file);

        cauth::steam::cloud::SteamCloudRequest pull_request;
        pull_request.app_id = 440;
        pull_request.access_token = "token";
        pull_request.local_root = temp_dir.string();
        pull_request.dry_run = true;
        pull_request.conflict_policy = cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins;
        const auto result = cauth::steam::cloud::pull_cloud_save(pull_request);
        if (!result.ok || result.transferred_count != 2 || result.conflict_count != 1 ||
            g_download_call_count != 0 ||
            result.message.find("would download 2 file(s)") == std::string::npos) {
            std::cerr << "pull dry-run should summarize planned downloads without fetching bytes\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-push-dry-run-delete");
        const auto local_path = temp_dir / "save1.sav";
        if (!write_text_file(local_path, "local-new")) {
            std::cerr << "failed to prepare push dry-run fixture\n";
            return 1;
        }

        g_upload_call_count = 0;
        g_captured_upload_files.clear();
        g_captured_delete_files.clear();
        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 3;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 1, 5, {}, 0, 0, "all", "older-sha"},
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 2, "save2.sav", 1, 5, {}, 0, 0, "all", "same-sha"},
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 3, "stale.sav", 1, 5, {}, 0, 0, "all", "stale-sha"},
        };
        if (!write_text_file(temp_dir / "save2.sav", "same")) {
            std::cerr << "failed to prepare unchanged push dry-run fixture\n";
            return 1;
        }
        const auto same_sha = cauth::core::hash::sha1_to_hex(
            cauth::core::hash::sha1_digest(std::vector<std::uint8_t>{'s', 'a', 'm', 'e'}));
        g_mock_upload_result = {true, "ok"};
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);
        cauth::steam::cloud::testing::set_upload_cloud_files_hook(&mock_upload_cloud_files);

        g_mock_list_result.files[1].file_sha = same_sha;

        cauth::steam::cloud::SteamCloudRequest push_request;
        push_request.app_id = 440;
        push_request.access_token = "token";
        push_request.local_root = temp_dir.string();
        push_request.dry_run = true;
        push_request.delete_remote_orphans = true;
        push_request.conflict_policy = cauth::steam::cloud::SteamCloudConflictPolicy::LocalWins;
        const auto result = cauth::steam::cloud::push_cloud_save(push_request);
        if (!result.ok || result.transferred_count != 1 || result.deleted_count != 1 ||
            result.skipped_count != 1 || result.conflict_count != 1 || g_upload_call_count != 0 ||
            result.message.find("would upload 1 file(s)") == std::string::npos ||
            result.message.find("1 unchanged") == std::string::npos ||
            result.message.find("deleted 1 remote file(s)") == std::string::npos) {
            std::cerr << "push dry-run should plan uploads and orphan deletes without sending them"
                      << " ok=" << result.ok
                      << " transferred=" << result.transferred_count
                      << " deleted=" << result.deleted_count
                      << " skipped=" << result.skipped_count
                      << " conflicts=" << result.conflict_count
                      << " upload_calls=" << g_upload_call_count
                      << " message=" << result.message << "\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-pull-local-wins");
        const auto local_path = temp_dir / "save1.sav";
        if (!write_text_file(local_path, "local")) {
            std::cerr << "failed to prepare pull local-wins fixture\n";
            return 1;
        }

        g_download_call_count = 0;
        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 9999999999ULL, 6, "mock://save1", 0, 0, "all", "deadbeef"}
        };
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);
        cauth::steam::cloud::testing::set_download_file_hook(&mock_download_file);

        cauth::steam::cloud::SteamCloudRequest pull_request;
        pull_request.app_id = 440;
        pull_request.access_token = "token";
        pull_request.local_root = temp_dir.string();
        pull_request.conflict_policy = cauth::steam::cloud::SteamCloudConflictPolicy::LocalWins;
        const auto result = cauth::steam::cloud::pull_cloud_save(pull_request);
        if (!result.ok || result.transferred_count != 0 || result.skipped_count != 1 ||
            result.conflict_count != 1 || g_download_call_count != 0 ||
            result.message.find("1 skipped by policy") == std::string::npos) {
            std::cerr << "pull should keep local file when conflict policy is local-wins\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-pull-skip-existing");
        const auto local_path = temp_dir / "save1.sav";
        if (!write_text_file(local_path, "local")) {
            std::cerr << "failed to prepare pull skip-existing fixture\n";
            return 1;
        }

        g_download_call_count = 0;
        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 9999999999ULL, 6, "mock://save1", 0, 0, "all", "deadbeef"}
        };
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);
        cauth::steam::cloud::testing::set_download_file_hook(&mock_download_file);

        cauth::steam::cloud::SteamCloudRequest pull_request;
        pull_request.app_id = 440;
        pull_request.access_token = "token";
        pull_request.local_root = temp_dir.string();
        pull_request.local_write_options.mode =
            cauth::core::platform::FileWriteMode::SkipExisting;
        const auto result = cauth::steam::cloud::pull_cloud_save(pull_request);
        if (!result.ok || result.transferred_count != 0 || result.skipped_count != 1 ||
            g_download_call_count != 0 ||
            result.message.find("1 existing") == std::string::npos ||
            read_text_file(local_path) != "local") {
            std::cerr << "pull skip-existing should skip local targets before download\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-pull-fail-if-exists");
        const auto local_path = temp_dir / "save1.sav";
        if (!write_text_file(local_path, "local")) {
            std::cerr << "failed to prepare pull fail-if-exists fixture\n";
            return 1;
        }

        g_download_call_count = 0;
        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 9999999999ULL, 6, "mock://save1", 0, 0, "all", "deadbeef"}
        };
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);
        cauth::steam::cloud::testing::set_download_file_hook(&mock_download_file);

        cauth::steam::cloud::SteamCloudRequest pull_request;
        pull_request.app_id = 440;
        pull_request.access_token = "token";
        pull_request.local_root = temp_dir.string();
        pull_request.local_write_options.mode =
            cauth::core::platform::FileWriteMode::FailIfExists;
        const auto result = cauth::steam::cloud::pull_cloud_save(pull_request);
        if (result.ok || g_download_call_count != 0 ||
            result.message.find("already exists") == std::string::npos) {
            std::cerr << "pull fail-if-exists should stop before download\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-pull-atomic-write");
        const auto local_path = temp_dir / "save1.sav";

        g_download_call_count = 0;
        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 10, 6, "mock://save1", 0, 0, "all", "deadbeef"}
        };
        g_mock_download_response = {};
        g_mock_download_response.ok = true;
        g_mock_download_response.bytes = {'r', 'e', 'm', 'o', 't', 'e'};
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);
        cauth::steam::cloud::testing::set_download_file_hook(&mock_download_file);

        cauth::steam::cloud::SteamCloudRequest pull_request;
        pull_request.app_id = 440;
        pull_request.access_token = "token";
        pull_request.local_root = temp_dir.string();
        pull_request.local_write_options.atomic_write = true;
        const auto result = cauth::steam::cloud::pull_cloud_save(pull_request);
        if (!result.ok || result.transferred_count != 1 || read_text_file(local_path) != "remote" ||
            std::filesystem::exists(temp_dir / "save1.sav.cauthdownload")) {
            std::cerr << "pull atomic-write should commit final file and remove temp file\n";
            return 1;
        }
    }

    {
        ScopedCloudHooksReset hooks_reset;
        const auto temp_dir = make_temp_dir("cauth-cloud-push-fail");
        const auto local_path = temp_dir / "save1.sav";
        if (!write_text_file(local_path, "local")) {
            std::cerr << "failed to prepare push fail-on-conflict fixture\n";
            return 1;
        }

        g_upload_call_count = 0;
        g_mock_list_result = {};
        g_mock_list_result.ok = true;
        g_mock_list_result.app_id = 440;
        g_mock_list_result.total_files = 1;
        g_mock_list_result.files = {
            cauth::steam::cloud::SteamCloudFileEntry{
                440, 1, "save1.sav", 9999999999ULL, 6, {}, 0, 0, "all", "remote-sha"}
        };
        cauth::steam::cloud::testing::set_list_remote_files_hook(&mock_list_remote_files);
        cauth::steam::cloud::testing::set_upload_cloud_files_hook(&mock_upload_cloud_files);

        cauth::steam::cloud::SteamCloudRequest push_request;
        push_request.app_id = 440;
        push_request.access_token = "token";
        push_request.local_root = temp_dir.string();
        push_request.conflict_policy = cauth::steam::cloud::SteamCloudConflictPolicy::FailOnConflict;
        const auto result = cauth::steam::cloud::push_cloud_save(push_request);
        if (result.ok || result.conflict_count != 1 ||
            result.message.find("conflict detected") == std::string::npos ||
            g_upload_call_count != 0) {
            std::cerr << "push should fail before upload when policy is fail-on-conflict\n";
            return 1;
        }
    }

    return 0;
}
