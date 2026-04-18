#include "steam/cloud/steam_cloud_application.hpp"

#include "core/platform/http_client.hpp"
#include "core/session/auth_session.hpp"
#include "core/hash/sha1.hpp"
#include "steam/auth/steam_session_identity.hpp"
#include "steam/cloud/steam_cloud_cm_service.hpp"
#include "steam/cloud/steam_cloud_service.hpp"
#include "steam/cloud/steam_cloud_test_hooks.hpp"
#include "steam/cloud/steam_cloud_upload_service.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace cauth::steam::cloud {

namespace {

struct SyncProgress {
    std::uint64_t local_file_count = 0;
    std::uint64_t remote_file_count = 0;
    std::uint64_t transferred_count = 0;
    std::uint64_t deleted_count = 0;
    std::uint64_t skipped_count = 0;
    std::uint64_t conflict_count = 0;
    std::uint64_t transferred_bytes = 0;
    std::uint64_t unchanged_count = 0;
    std::uint64_t filtered_count = 0;
    std::uint64_t policy_skipped_count = 0;
};

testing::ListRemoteFilesHook g_list_remote_files_hook = nullptr;
testing::DownloadFileHook g_download_file_hook = nullptr;
testing::UploadCloudFilesHook g_upload_cloud_files_hook = nullptr;
thread_local SteamCloudTransferProgressHook g_transfer_progress_hook = nullptr;
thread_local SteamCloudTransferCancelHook g_transfer_cancel_hook = nullptr;
thread_local void* g_transfer_hook_user_data = nullptr;

SteamCloudResult make_result(const SteamCloudRequest& request,
                            SteamCloudDirection direction,
                            bool ok,
                            std::string message,
                            SyncProgress progress = {}) {
    SteamCloudResult result;
    result.ok = ok;
    result.app_id = request.app_id;
    result.direction = direction;
    result.conflict_policy = request.conflict_policy;
    result.local_file_count = progress.local_file_count;
    result.remote_file_count = progress.remote_file_count;
    result.transferred_count = progress.transferred_count;
    result.deleted_count = progress.deleted_count;
    result.skipped_count = progress.skipped_count;
    result.conflict_count = progress.conflict_count;
    result.transferred_bytes = progress.transferred_bytes;
    result.message = std::move(message);
    return result;
}

SteamCloudResult make_canceled_result(const SteamCloudRequest& request,
                                      SteamCloudDirection direction,
                                      const SyncProgress& progress,
                                      std::string_view phase) {
    std::string message = "operation canceled";
    if (!phase.empty()) {
        message += " during ";
        message += phase;
    }
    return make_result(request, direction, false, std::move(message), progress);
}

bool is_transfer_canceled() {
    return g_transfer_cancel_hook != nullptr && g_transfer_cancel_hook(g_transfer_hook_user_data);
}

void report_transfer_progress(const SteamCloudTransferProgress& progress) {
    if (g_transfer_progress_hook == nullptr) {
        return;
    }
    g_transfer_progress_hook(progress, g_transfer_hook_user_data);
}

std::string normalize_slashes(std::string value) {
    for (auto& ch : value) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    return value;
}

bool starts_with_path_prefix(std::string_view path, std::string_view prefix) {
    if (prefix.empty()) {
        return true;
    }
    if (path.size() < prefix.size()) {
        return false;
    }
    if (path.substr(0, prefix.size()) != prefix) {
        return false;
    }
    return path.size() == prefix.size() || path[prefix.size()] == '/';
}

std::string strip_path_prefix(std::string_view path, std::string_view prefix) {
    if (!starts_with_path_prefix(path, prefix)) {
        return {};
    }
    if (path.size() == prefix.size()) {
        return {};
    }
    auto relative = std::string{path.substr(prefix.size())};
    while (!relative.empty() && relative.front() == '/') {
        relative.erase(relative.begin());
    }
    return relative;
}

bool is_safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    for (const auto& part : path) {
        if (part == ".." || part == "/" || part == "\\") {
            return false;
        }
    }
    return true;
}

bool write_bytes_to_path(const std::filesystem::path& output_path,
                         const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(output_path.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream output{output_path, std::ios::binary};
    if (!output) {
        return false;
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(output);
}

std::string describe_skip_breakdown(const SyncProgress& progress) {
    std::string summary = std::to_string(progress.skipped_count) + " file(s)";
    if (progress.unchanged_count == 0 && progress.filtered_count == 0 &&
        progress.policy_skipped_count == 0) {
        return summary;
    }

    summary += " (";
    bool first = true;
    auto append_part = [&](std::uint64_t count, std::string_view label) {
        if (count == 0) {
            return;
        }
        if (!first) {
            summary += ", ";
        }
        first = false;
        summary += std::to_string(count);
        summary += " ";
        summary += label;
    };
    append_part(progress.unchanged_count, "unchanged");
    append_part(progress.filtered_count, "filtered");
    append_part(progress.policy_skipped_count, "skipped by policy");
    summary += ")";
    return summary;
}

std::string describe_download_summary(const SyncProgress& progress, bool dry_run) {
    return std::string{dry_run ? "would download " : "downloaded "} +
           std::to_string(progress.transferred_count) + " file(s), skipped " +
           describe_skip_breakdown(progress) + ", conflicts=" +
           std::to_string(progress.conflict_count) + ", bytes=" +
           std::to_string(progress.transferred_bytes);
}

std::string trim_slashes(std::string value) {
    while (!value.empty() && (value.front() == '/' || value.front() == '\\')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == '/' || value.back() == '\\')) {
        value.pop_back();
    }
    return value;
}

std::string join_remote_filename(std::string_view prefix, const std::filesystem::path& relative_path) {
    const auto relative = normalize_slashes(relative_path.generic_string());
    const auto normalized_prefix = trim_slashes(normalize_slashes(std::string{prefix}));
    if (normalized_prefix.empty()) {
        return relative;
    }
    if (relative.empty()) {
        return normalized_prefix;
    }
    return normalized_prefix + "/" + relative;
}

std::string lowercase_ascii(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::optional<std::vector<std::uint8_t>> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        return std::nullopt;
    }
    const auto size = input.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!input && !bytes.empty()) {
        return std::nullopt;
    }
    return bytes;
}

std::string describe_push_summary(const SyncProgress& progress, bool dry_run) {
    return std::string{dry_run ? "would upload " : "uploaded "} +
           std::to_string(progress.transferred_count) + " file(s), deleted " +
           std::to_string(progress.deleted_count) + " remote file(s), skipped " +
           describe_skip_breakdown(progress) + ", conflicts=" +
           std::to_string(progress.conflict_count) + ", bytes=" +
           std::to_string(progress.transferred_bytes);
}

const char* verify_status_name(SteamCloudVerifyStatus status) {
    switch (status) {
    case SteamCloudVerifyStatus::Ok:
        return "OK";
    case SteamCloudVerifyStatus::MissingLocal:
        return "MISSING";
    case SteamCloudVerifyStatus::Mismatched:
        return "MISMATCH";
    case SteamCloudVerifyStatus::SizeOnly:
        return "SIZE_ONLY";
    case SteamCloudVerifyStatus::ExtraLocal:
        return "EXTRA_LOCAL";
    }
    return "UNKNOWN";
}

std::string describe_verify_summary(const SteamCloudVerifyResult& result) {
    return "verified " + std::to_string(result.checked_count) + " file(s), ok=" +
           std::to_string(result.ok_count) + ", missing=" +
           std::to_string(result.missing_count) + ", mismatched=" +
           std::to_string(result.mismatched_count) + ", size_only=" +
           std::to_string(result.size_only_count) + ", filtered_out=" +
           std::to_string(result.filtered_out_count) + ", extra_local=" +
           std::to_string(result.extra_local_count) + ", total=" +
           std::to_string(result.total_count);
}

bool should_use_cm_cloud_backend(const SteamCloudRequest& request) {
    if (request.backend == SteamCloudBackend::CmCloud) {
        return true;
    }
    if (request.backend == SteamCloudBackend::WebApi) {
        return false;
    }
    if (!request.web_cookie_header.empty()) {
        return false;
    }
    if (request.session_type == cauth::steam::auth::kSteamSessionTypeSteamClient) {
        return true;
    }
    if (request.session_type == cauth::steam::auth::kSteamSessionTypeWebBrowser ||
        request.session_type == cauth::steam::auth::kSteamSessionTypeMobileApp) {
        return false;
    }
    return request.access_token.empty();
}

std::optional<std::uint64_t> file_time_to_epoch_seconds(
    const std::filesystem::file_time_type& value) {
    using namespace std::chrono;
    const auto system_now = system_clock::now();
    const auto file_now = std::filesystem::file_time_type::clock::now();
    const auto system_time =
        time_point_cast<system_clock::duration>(value - file_now + system_now);
    const auto seconds = duration_cast<std::chrono::seconds>(system_time.time_since_epoch()).count();
    if (seconds < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(seconds);
}

std::optional<std::uint64_t> try_get_last_write_time_seconds(const std::filesystem::path& path) {
    std::error_code ec;
    const auto value = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::nullopt;
    }
    return file_time_to_epoch_seconds(value);
}

struct ExistingLocalFileState {
    bool exists = false;
    std::string file_sha;
    std::uint64_t timestamp = 0;
};

std::optional<ExistingLocalFileState> load_existing_local_file_state(const std::filesystem::path& path) {
    std::error_code ec;
    const auto exists = std::filesystem::exists(path, ec);
    if (ec) {
        return std::nullopt;
    }
    if (!exists) {
        return ExistingLocalFileState{};
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return std::nullopt;
    }

    const auto bytes = read_file_bytes(path);
    if (!bytes.has_value()) {
        return std::nullopt;
    }

    ExistingLocalFileState state;
    state.exists = true;
    state.file_sha = cauth::core::hash::sha1_to_hex(cauth::core::hash::sha1_digest(*bytes));
    state.timestamp = try_get_last_write_time_seconds(path).value_or(0);
    return state;
}

std::optional<std::vector<SteamCloudFileEntry>> list_all_remote_files(const SteamCloudRequest& request,
                                                                      std::uint32_t page_size,
                                                                      bool extended_details,
                                                                      std::string& error_message) {
    if (page_size == 0) {
        page_size = 500;
    }

    std::vector<SteamCloudFileEntry> files;
    std::uint32_t start_index = 0;
    std::uint64_t expected_total = 0;
    bool saw_total = false;
    while (true) {
        const auto page = list_remote_files(request, page_size, start_index, extended_details);
        if (!page.ok) {
            error_message = page.message;
            return std::nullopt;
        }
        if (!saw_total) {
            expected_total = page.total_files;
            saw_total = true;
        }
        if (page.files.empty()) {
            break;
        }
        files.insert(files.end(), page.files.begin(), page.files.end());
        start_index += static_cast<std::uint32_t>(page.files.size());
        if (page.files.size() < page_size) {
            break;
        }
        if (expected_total != 0 && files.size() >= expected_total) {
            break;
        }
    }
    return files;
}

bool should_transfer_pull_conflict(const SteamCloudRequest& request,
                                   const ExistingLocalFileState& local_state,
                                   const SteamCloudFileEntry& remote_file,
                                   std::string& error_message) {
    switch (request.conflict_policy) {
    case SteamCloudConflictPolicy::LocalWins:
        return false;
    case SteamCloudConflictPolicy::RemoteWins:
    case SteamCloudConflictPolicy::Default:
        return true;
    case SteamCloudConflictPolicy::NewerWins:
        return remote_file.timestamp > local_state.timestamp;
    case SteamCloudConflictPolicy::FailOnConflict:
        error_message = "conflict detected for " + remote_file.filename;
        return false;
    }
    return true;
}

bool should_transfer_push_conflict(const SteamCloudRequest& request,
                                   std::uint64_t local_timestamp,
                                   const SteamCloudFileEntry& remote_file,
                                   std::string& error_message) {
    switch (request.conflict_policy) {
    case SteamCloudConflictPolicy::LocalWins:
    case SteamCloudConflictPolicy::Default:
        return true;
    case SteamCloudConflictPolicy::RemoteWins:
        return false;
    case SteamCloudConflictPolicy::NewerWins:
        return local_timestamp > remote_file.timestamp;
    case SteamCloudConflictPolicy::FailOnConflict:
        error_message = "conflict detected for " + remote_file.filename;
        return false;
    }
    return true;
}

void print_verify_result_summary(const SteamCloudVerifyResult& result, std::ostream& out) {
    out << "Steam cloud verify: " << (result.clean() ? "clean" : "issues found") << '\n'
        << "AppID: " << result.app_id << '\n'
        << "Checked: " << result.checked_count << '\n'
        << "OK: " << result.ok_count << '\n'
        << "Missing: " << result.missing_count << '\n'
        << "Mismatched: " << result.mismatched_count << '\n'
        << "Size-only: " << result.size_only_count << '\n'
        << "Filtered out: " << result.filtered_out_count << '\n'
        << "Extra local: " << result.extra_local_count << '\n'
        << "Total remote: " << result.total_count << '\n'
        << "Message: " << result.message << '\n';
}

SteamCloudRequest with_saved_auth_session(core::session::SessionRepository& store,
                                         const SteamCloudRequest& request) {
    auto effective = request;
    const auto session = store.load_auth_session();
    if (session.has_value()) {
        const bool is_web_browser_session =
            session->session_type == cauth::steam::auth::kSteamSessionTypeWebBrowser;

        if (effective.access_token.empty() && !is_web_browser_session) {
            try {
                effective.access_token = session->access_token;
            } catch (const std::exception& ex) {
                throw std::runtime_error(
                    "copy saved access token failed: " + std::string{ex.what()} +
                    " [saved_len=" + std::to_string(session->access_token.size()) + "]");
            }
        }
        if (effective.refresh_token.empty()) {
            try {
                effective.refresh_token = session->refresh_token;
            } catch (const std::exception& ex) {
                throw std::runtime_error(
                    "copy saved refresh token failed: " + std::string{ex.what()} +
                    " [saved_len=" + std::to_string(session->refresh_token.size()) + "]");
            }
        }
        if (effective.session_type.empty()) {
            try {
                effective.session_type = session->session_type;
            } catch (const std::exception& ex) {
                throw std::runtime_error(
                    "copy saved session type failed: " + std::string{ex.what()} +
                    " [saved_len=" + std::to_string(session->session_type.size()) + "]");
            }
        }
        if (effective.steam_id == 0) {
            effective.steam_id = cauth::steam::auth::steam_id(*session);
        }
    }
    return effective;
}

const char* conflict_policy_name(SteamCloudConflictPolicy policy) {
    switch (policy) {
    case SteamCloudConflictPolicy::LocalWins:
        return "local-wins";
    case SteamCloudConflictPolicy::RemoteWins:
        return "remote-wins";
    case SteamCloudConflictPolicy::NewerWins:
        return "newer-wins";
    case SteamCloudConflictPolicy::FailOnConflict:
        return "fail";
    case SteamCloudConflictPolicy::Default:
    default:
        return "default";
    }
}

void print_sync_result_summary(const SteamCloudResult& result, std::ostream& out) {
    out << "Steam cloud result: " << (result.ok ? "ok" : "failed") << '\n'
        << "AppID: " << result.app_id << '\n'
        << "Direction: " << (result.direction == SteamCloudDirection::Pull ? "pull" : "push") << '\n'
        << "Conflict policy: " << conflict_policy_name(result.conflict_policy) << '\n'
        << "Local files: " << result.local_file_count << '\n'
        << "Remote files: " << result.remote_file_count << '\n'
        << "Transferred: " << result.transferred_count << '\n'
        << "Deleted: " << result.deleted_count << '\n'
        << "Skipped: " << result.skipped_count << '\n'
        << "Conflicts: " << result.conflict_count << '\n'
        << "Bytes: " << result.transferred_bytes << '\n'
        << "Message: " << result.message << '\n';
}

} // namespace

void set_current_thread_steam_cloud_transfer_hooks(
    SteamCloudTransferProgressHook progress_hook,
    SteamCloudTransferCancelHook cancel_hook,
    void* user_data) {
    g_transfer_progress_hook = progress_hook;
    g_transfer_cancel_hook = cancel_hook;
    g_transfer_hook_user_data = user_data;
}

void clear_current_thread_steam_cloud_transfer_hooks() {
    g_transfer_progress_hook = nullptr;
    g_transfer_cancel_hook = nullptr;
    g_transfer_hook_user_data = nullptr;
}

SteamCloudFileListResult list_remote_files(const SteamCloudRequest& request,
                                           std::uint32_t count,
                                           std::uint32_t start_index,
                                           bool extended_details) {
    if (g_list_remote_files_hook != nullptr) {
        return g_list_remote_files_hook(request, count, start_index, extended_details);
    }
    return fetch_remote_file_list(request, count, start_index, extended_details);
}

SteamCloudVerifyResult verify_cloud_local_files(const SteamCloudRequest& request,
                                                bool include_extra_local,
                                                std::uint32_t page_size,
                                                bool extended_details) {
    SteamCloudVerifyResult result;
    result.app_id = request.app_id;
    result.include_extra_local = include_extra_local;

    std::string auth_error;
    const auto effective_request = materialize_cloud_web_api_auth(request, &auth_error);
    result.app_id = effective_request.app_id;

    if (effective_request.app_id == 0) {
        result.fatal_error = true;
        result.message = "app_id is required";
        return result;
    }
    if (effective_request.local_root.empty()) {
        result.fatal_error = true;
        result.message = "local_root is required";
        return result;
    }

    std::error_code ec;
    const std::filesystem::path local_root{effective_request.local_root};
    if (!std::filesystem::exists(local_root, ec)) {
        result.fatal_error = true;
        result.message = "local_root does not exist";
        if (ec) {
            result.message += ": " + ec.message();
        }
        return result;
    }
    if (!std::filesystem::is_directory(local_root, ec) || ec) {
        result.fatal_error = true;
        result.message = "local_root must be a directory";
        if (ec) {
            result.message += ": " + ec.message();
        }
        return result;
    }

    std::string list_error;
    const auto remote_files =
        list_all_remote_files(effective_request, page_size, extended_details, list_error);
    if (!remote_files.has_value()) {
        result.fatal_error = true;
        result.message = list_error.empty() ? auth_error : list_error;
        return result;
    }

    result.total_count = remote_files->size();
    std::unordered_set<std::string> matched_remote_names;
    matched_remote_names.reserve(remote_files->size());

    const auto remote_prefix = normalize_slashes(effective_request.remote_root);
    for (const auto& remote_file : *remote_files) {
        const auto normalized_name = normalize_slashes(remote_file.filename);
        if (!starts_with_path_prefix(normalized_name, remote_prefix)) {
            ++result.filtered_out_count;
            continue;
        }

        auto relative_name = remote_prefix.empty() ? normalized_name
                                                   : strip_path_prefix(normalized_name, remote_prefix);
        if (relative_name.empty()) {
            ++result.filtered_out_count;
            continue;
        }

        const auto relative_path = std::filesystem::path{relative_name}.lexically_normal();
        if (!is_safe_relative_path(relative_path)) {
            result.entries.push_back(SteamCloudVerifyEntry{
                normalized_name,
                {},
                SteamCloudVerifyStatus::Mismatched,
                remote_file.file_size,
                remote_file.timestamp,
                remote_file.file_sha,
                0,
                {},
                "unsafe relative path",
            });
            ++result.mismatched_count;
            ++result.checked_count;
            continue;
        }

        matched_remote_names.insert(normalized_name);

        SteamCloudVerifyEntry entry;
        entry.remote_filename = normalized_name;
        entry.local_path = (local_root / relative_path).string();
        entry.remote_size = remote_file.file_size;
        entry.remote_timestamp = remote_file.timestamp;
        entry.remote_sha = lowercase_ascii(remote_file.file_sha);

        const auto local_state = load_existing_local_file_state(local_root / relative_path);
        if (!local_state.has_value()) {
            entry.status = SteamCloudVerifyStatus::Mismatched;
            entry.reason = "failed to inspect local file";
            result.entries.push_back(std::move(entry));
            ++result.checked_count;
            ++result.mismatched_count;
            continue;
        }

        ++result.checked_count;
        if (!local_state->exists) {
            entry.status = SteamCloudVerifyStatus::MissingLocal;
            entry.reason = "local file missing";
            result.entries.push_back(std::move(entry));
            ++result.missing_count;
            continue;
        }

        const auto local_path = std::filesystem::path{entry.local_path};
        const auto local_size = std::filesystem::file_size(local_path, ec);
        if (ec) {
            entry.status = SteamCloudVerifyStatus::Mismatched;
            entry.reason = "failed to read local file size: " + ec.message();
            ec.clear();
            result.entries.push_back(std::move(entry));
            ++result.mismatched_count;
            continue;
        }
        entry.local_size = local_size;
        entry.local_sha = lowercase_ascii(local_state->file_sha);

        if (entry.local_size != remote_file.file_size) {
            entry.status = SteamCloudVerifyStatus::Mismatched;
            entry.reason = "file size differs";
            result.entries.push_back(std::move(entry));
            ++result.mismatched_count;
            continue;
        }

        if (entry.remote_sha.empty()) {
            entry.status = SteamCloudVerifyStatus::SizeOnly;
            entry.reason = "remote SHA unavailable; size-only verification";
            result.entries.push_back(std::move(entry));
            ++result.ok_count;
            ++result.size_only_count;
            continue;
        }

        if (entry.local_sha != entry.remote_sha) {
            entry.status = SteamCloudVerifyStatus::Mismatched;
            entry.reason = "SHA-1 differs";
            result.entries.push_back(std::move(entry));
            ++result.mismatched_count;
            continue;
        }

        entry.status = SteamCloudVerifyStatus::Ok;
        entry.reason = "sha matched";
        result.entries.push_back(std::move(entry));
        ++result.ok_count;
    }

    if (include_extra_local) {
        for (const auto& disk_entry : std::filesystem::recursive_directory_iterator(local_root, ec)) {
            if (ec) {
                result.fatal_error = true;
                result.message = "failed to enumerate local_root: " + ec.message();
                return result;
            }
            if (!disk_entry.is_regular_file()) {
                continue;
            }

            const auto relative = std::filesystem::relative(disk_entry.path(), local_root, ec);
            if (ec || relative.empty()) {
                result.fatal_error = true;
                result.message = "failed to resolve local file path";
                if (ec) {
                    result.message += ": " + ec.message();
                }
                return result;
            }

            const auto remote_name = normalize_slashes(join_remote_filename(remote_prefix, relative));
            if (matched_remote_names.find(remote_name) != matched_remote_names.end()) {
                continue;
            }

            SteamCloudVerifyEntry extra_entry;
            extra_entry.remote_filename = remote_name;
            extra_entry.local_path = disk_entry.path().string();
            extra_entry.status = SteamCloudVerifyStatus::ExtraLocal;
            extra_entry.reason = "local file does not exist remotely";
            extra_entry.local_size = std::filesystem::file_size(disk_entry.path(), ec);
            if (ec) {
                extra_entry.reason = "local file does not exist remotely; failed to read size: " +
                                     ec.message();
                ec.clear();
            }
            const auto local_state = load_existing_local_file_state(disk_entry.path());
            if (local_state.has_value() && local_state->exists) {
                extra_entry.local_sha = lowercase_ascii(local_state->file_sha);
            }
            result.entries.push_back(std::move(extra_entry));
            ++result.extra_local_count;
        }
    }

    result.ok = true;
    result.message = describe_verify_summary(result);
    return result;
}

SteamCloudResult pull_cloud_save(const SteamCloudRequest& request) {
    std::string auth_error;
    const auto effective_request = materialize_cloud_web_api_auth(request, &auth_error);
    SyncProgress progress;
    report_transfer_progress(
        SteamCloudTransferProgress{SteamCloudTransferKind::Pull, "Preparing pull", {}});
    if (effective_request.app_id == 0) {
        return make_result(
            effective_request, SteamCloudDirection::Pull, false, "app_id is required");
    }
    if (effective_request.local_root.empty()) {
        return make_result(
            effective_request, SteamCloudDirection::Pull, false, "local_root is required");
    }
    if (is_transfer_canceled()) {
        return make_canceled_result(
            effective_request, SteamCloudDirection::Pull, progress, "preparation");
    }

    report_transfer_progress(
        SteamCloudTransferProgress{SteamCloudTransferKind::Pull, "Listing remote files", {}});
    const auto list_result = list_remote_files(effective_request);
    if (!list_result.ok) {
        return make_result(
            effective_request,
            SteamCloudDirection::Pull,
            false,
            list_result.message.empty() ? auth_error : list_result.message);
    }
    progress.remote_file_count = list_result.files.size();
    std::uint64_t total_remote_bytes = 0;
    for (const auto& file : list_result.files) {
        total_remote_bytes += file.file_size;
    }

    const auto remote_prefix = normalize_slashes(effective_request.remote_root);
    const auto output_root = std::filesystem::path{effective_request.local_root};
    const auto total_remote_files = static_cast<std::uint64_t>(list_result.files.size());
    auto report_pull_state = [&](std::string phase, std::string target = {}) {
        report_transfer_progress(SteamCloudTransferProgress{
            SteamCloudTransferKind::Pull,
            std::move(phase),
            std::move(target),
            progress.transferred_count + progress.skipped_count,
            total_remote_files,
            progress.transferred_bytes,
            total_remote_bytes,
        });
    };
    report_pull_state("Filtering remote files");

    for (const auto& file : list_result.files) {
        if (is_transfer_canceled()) {
            return make_canceled_result(
                effective_request, SteamCloudDirection::Pull, progress, "pull");
        }
        const auto normalized_name = normalize_slashes(file.filename);
        if (!starts_with_path_prefix(normalized_name, remote_prefix)) {
            ++progress.filtered_count;
            ++progress.skipped_count;
            report_pull_state("Skipping filtered file", normalized_name);
            continue;
        }

        auto relative_name = remote_prefix.empty() ? normalized_name
                                                   : strip_path_prefix(normalized_name, remote_prefix);
        if (relative_name.empty()) {
            ++progress.filtered_count;
            ++progress.skipped_count;
            report_pull_state("Skipping filtered file", normalized_name);
            continue;
        }

        const auto relative_path = std::filesystem::path{relative_name}.lexically_normal();
        if (!is_safe_relative_path(relative_path)) {
            ++progress.filtered_count;
            ++progress.skipped_count;
            report_pull_state("Skipping unsafe path", normalized_name);
            continue;
        }

        const auto output_path = output_root / relative_path;
        const auto local_state = load_existing_local_file_state(output_path);
        if (!local_state.has_value()) {
            return make_result(
                effective_request,
                SteamCloudDirection::Pull,
                false,
                "failed to inspect local file " + output_path.string(),
                progress);
        }

        if (local_state->exists) {
            ++progress.local_file_count;
            if (!file.file_sha.empty() &&
                lowercase_ascii(local_state->file_sha) == lowercase_ascii(file.file_sha)) {
                ++progress.unchanged_count;
                ++progress.skipped_count;
                report_pull_state("Skipping unchanged file", normalized_name);
                continue;
            }

            ++progress.conflict_count;
            std::string conflict_error;
            const auto should_download =
                should_transfer_pull_conflict(effective_request, *local_state, file, conflict_error);
            if (effective_request.conflict_policy == SteamCloudConflictPolicy::FailOnConflict) {
                return make_result(
                    effective_request, SteamCloudDirection::Pull, false, conflict_error, progress);
            }
            if (!should_download) {
                ++progress.policy_skipped_count;
                ++progress.skipped_count;
                report_pull_state("Skipping by conflict policy", normalized_name);
                continue;
            }
        }

        if (effective_request.dry_run) {
            ++progress.transferred_count;
            progress.transferred_bytes += file.file_size;
            report_pull_state("Dry-run scheduled download", normalized_name);
            continue;
        }

        report_pull_state("Downloading file", normalized_name);
        const auto download_result = g_download_file_hook != nullptr
                                         ? g_download_file_hook(effective_request, file)
                                         : download_remote_file(effective_request, file);
        if (!download_result.ok) {
            return make_result(
                effective_request,
                SteamCloudDirection::Pull,
                false,
                "failed to download " + normalized_name + ": " + download_result.message,
                progress);
        }

        if (is_transfer_canceled()) {
            return make_canceled_result(
                effective_request, SteamCloudDirection::Pull, progress, normalized_name);
        }
        report_pull_state("Writing file", normalized_name);
        if (!write_bytes_to_path(output_path, download_result.bytes)) {
            return make_result(
                effective_request,
                SteamCloudDirection::Pull,
                false,
                "failed to write " + output_path.string(),
                progress);
        }

        ++progress.transferred_count;
        progress.transferred_bytes += download_result.bytes.size();
        report_pull_state("Downloaded file", normalized_name);
    }

    report_transfer_progress(SteamCloudTransferProgress{
        SteamCloudTransferKind::Pull,
        "Pull complete",
        {},
        progress.transferred_count + progress.skipped_count,
        total_remote_files,
        progress.transferred_bytes,
        total_remote_bytes,
    });
    return make_result(
        effective_request,
        SteamCloudDirection::Pull,
        true,
        describe_download_summary(progress, effective_request.dry_run),
        progress);
}

SteamCloudResult push_cloud_save(const SteamCloudRequest& request) {
    std::string auth_error;
    const auto effective_request = materialize_cloud_web_api_auth(request, &auth_error);
    SyncProgress progress;
    report_transfer_progress(
        SteamCloudTransferProgress{SteamCloudTransferKind::Push, "Preparing push", {}});
    if (effective_request.app_id == 0) {
        return make_result(
            effective_request, SteamCloudDirection::Push, false, "app_id is required");
    }
    if (effective_request.local_root.empty()) {
        return make_result(
            effective_request, SteamCloudDirection::Push, false, "local_root is required");
    }
    const auto use_cm_upload = should_use_cm_cloud_backend(effective_request);
    if (!use_cm_upload && effective_request.access_token.empty() &&
        effective_request.web_cookie_header.empty()) {
        return make_result(
            effective_request,
            SteamCloudDirection::Push,
            false,
            auth_error.empty() ? "access token with write_cloud scope or web cookie session is required"
                               : auth_error);
    }
    if (is_transfer_canceled()) {
        return make_canceled_result(
            effective_request, SteamCloudDirection::Push, progress, "preparation");
    }

    const auto input_root = std::filesystem::path{effective_request.local_root};
    std::error_code ec;
    if (!std::filesystem::exists(input_root, ec) || ec) {
        return make_result(
            effective_request, SteamCloudDirection::Push, false, "local_root does not exist");
    }
    if (!std::filesystem::is_directory(input_root, ec) || ec) {
        return make_result(
            effective_request, SteamCloudDirection::Push, false, "local_root must be a directory");
    }

    report_transfer_progress(
        SteamCloudTransferProgress{SteamCloudTransferKind::Push, "Listing remote files", {}});
    const auto list_result = list_remote_files(effective_request);
    if (!list_result.ok) {
        return make_result(
            effective_request,
            SteamCloudDirection::Push,
            false,
            list_result.message.empty() ? auth_error : list_result.message);
    }
    progress.remote_file_count = list_result.files.size();

    const auto remote_prefix = trim_slashes(normalize_slashes(effective_request.remote_root));
    std::unordered_map<std::string, SteamCloudFileEntry> remote_file_by_name;
    remote_file_by_name.reserve(list_result.files.size());
    for (const auto& file : list_result.files) {
        const auto normalized_name = normalize_slashes(file.filename);
        if (!starts_with_path_prefix(normalized_name, remote_prefix)) {
            continue;
        }
        auto normalized_file = file;
        normalized_file.filename = normalized_name;
        normalized_file.file_sha = lowercase_ascii(normalized_file.file_sha);
        remote_file_by_name[normalized_name] = std::move(normalized_file);
    }

    std::vector<SteamCloudUploadFile> files_to_upload;
    std::unordered_set<std::string> local_remote_filenames;
    local_remote_filenames.reserve(remote_file_by_name.size());
    report_transfer_progress(
        SteamCloudTransferProgress{SteamCloudTransferKind::Push, "Scanning local files", {}});
    for (const auto& entry : std::filesystem::recursive_directory_iterator(input_root, ec)) {
        if (is_transfer_canceled()) {
            return make_canceled_result(
                effective_request, SteamCloudDirection::Push, progress, "local scan");
        }
        if (ec) {
            return make_result(
                effective_request,
                SteamCloudDirection::Push,
                false,
                "failed to enumerate local_root",
                progress);
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        ++progress.local_file_count;
        report_transfer_progress(SteamCloudTransferProgress{
            SteamCloudTransferKind::Push,
            "Scanning local files",
            entry.path().string(),
            progress.local_file_count,
            0,
            0,
            0,
        });

        const auto relative = std::filesystem::relative(entry.path(), input_root, ec);
        if (ec || relative.empty()) {
            return make_result(
                effective_request,
                SteamCloudDirection::Push,
                false,
                "failed to resolve relative file path",
                progress);
        }

        const auto bytes = read_file_bytes(entry.path());
        if (!bytes.has_value()) {
            return make_result(
                effective_request,
                SteamCloudDirection::Push,
                false,
                "failed to read " + entry.path().string(),
                progress);
        }

        SteamCloudUploadFile file;
        file.local_path = entry.path().string();
        file.filename = join_remote_filename(effective_request.remote_root, relative);
        file.file_size = static_cast<std::uint32_t>(bytes->size());
        file.bytes = *bytes;
        file.file_sha = cauth::core::hash::sha1_to_hex(cauth::core::hash::sha1_digest(file.bytes));
        file.platforms_to_sync = {"all"};
        local_remote_filenames.insert(file.filename);

        const auto remote_it = remote_file_by_name.find(file.filename);
        if (remote_it != remote_file_by_name.end() &&
            remote_it->second.file_sha == lowercase_ascii(file.file_sha)) {
            ++progress.unchanged_count;
            ++progress.skipped_count;
            continue;
        }

        if (remote_it != remote_file_by_name.end()) {
            ++progress.conflict_count;
            std::string conflict_error;
            const auto local_timestamp = try_get_last_write_time_seconds(entry.path()).value_or(0);
            const auto should_upload = should_transfer_push_conflict(
                effective_request, local_timestamp, remote_it->second, conflict_error);
            if (effective_request.conflict_policy == SteamCloudConflictPolicy::FailOnConflict) {
                return make_result(
                    effective_request, SteamCloudDirection::Push, false, conflict_error, progress);
            }
            if (!should_upload) {
                ++progress.policy_skipped_count;
                ++progress.skipped_count;
                continue;
            }
        }

        progress.transferred_bytes += bytes->size();
        files_to_upload.push_back(std::move(file));
    }

    std::vector<std::string> files_to_delete;
    if (effective_request.delete_remote_orphans) {
        files_to_delete.reserve(remote_file_by_name.size());
        for (const auto& [filename, remote_file] : remote_file_by_name) {
            if (is_transfer_canceled()) {
                return make_canceled_result(
                    effective_request, SteamCloudDirection::Push, progress, "delete planning");
            }
            (void)remote_file;
            if (local_remote_filenames.find(filename) == local_remote_filenames.end()) {
                files_to_delete.push_back(filename);
            }
        }
    }

    if (effective_request.dry_run) {
        progress.transferred_count = files_to_upload.size();
        progress.deleted_count = files_to_delete.size();
        report_transfer_progress(SteamCloudTransferProgress{
            SteamCloudTransferKind::Push,
            "Dry-run prepared upload plan",
            {},
            progress.transferred_count + progress.deleted_count,
            progress.transferred_count + progress.deleted_count,
            progress.transferred_bytes,
            progress.transferred_bytes,
        });
        return make_result(
            effective_request,
            SteamCloudDirection::Push,
            true,
            describe_push_summary(progress, true),
            progress);
    }

    if (files_to_upload.empty() && files_to_delete.empty()) {
        report_transfer_progress(SteamCloudTransferProgress{
            SteamCloudTransferKind::Push,
            "Nothing to upload",
            {},
            0,
            0,
            0,
            0,
        });
        return make_result(
            effective_request,
            SteamCloudDirection::Push,
            true,
            describe_push_summary(progress, false),
            progress);
    }

    if (is_transfer_canceled()) {
        return make_canceled_result(
            effective_request, SteamCloudDirection::Push, progress, "upload preparation");
    }
    const auto total_push_steps = static_cast<std::uint64_t>(files_to_upload.size() + files_to_delete.size());
    report_transfer_progress(SteamCloudTransferProgress{
        SteamCloudTransferKind::Push,
        "Uploading files",
        {},
        0,
        total_push_steps,
        0,
        progress.transferred_bytes,
    });
    const auto upload_result =
        use_cm_upload
            ? upload_cloud_files_via_cm(effective_request, "CAuth", files_to_upload, files_to_delete)
            : (g_upload_cloud_files_hook != nullptr
                     ? g_upload_cloud_files_hook(
                           SteamCloudWebAuthContext{
                               effective_request.access_token,
                               effective_request.web_cookie_header,
                               effective_request.store_cookie_header,
                           },
                           effective_request.app_id,
                           "CAuth",
                           files_to_upload,
                           files_to_delete)
                     : upload_cloud_files(
                           SteamCloudWebAuthContext{
                               effective_request.access_token,
                               effective_request.web_cookie_header,
                               effective_request.store_cookie_header,
                           },
                           effective_request.app_id,
                           "CAuth",
                           files_to_upload,
                           files_to_delete));
    if (!upload_result.ok) {
        return make_result(
            effective_request, SteamCloudDirection::Push, false, upload_result.message, progress);
    }

    progress.transferred_count = files_to_upload.size();
    progress.deleted_count = files_to_delete.size();
    report_transfer_progress(SteamCloudTransferProgress{
        SteamCloudTransferKind::Push,
        "Push complete",
        {},
        progress.transferred_count + progress.deleted_count,
        total_push_steps,
        progress.transferred_bytes,
        progress.transferred_bytes,
    });
    return make_result(
        effective_request,
        SteamCloudDirection::Push,
        true,
        describe_push_summary(progress, false),
        progress);
}

int print_remote_files(core::session::SessionRepository& store,
                       const SteamCloudRequest& request,
                       std::uint32_t count,
                       std::uint32_t start_index,
                       bool extended_details,
                       std::ostream& out,
                       std::ostream& err) {
    SteamCloudRequest effective_request;
    try {
        effective_request = with_saved_auth_session(store, request);
    } catch (const std::exception& ex) {
        err << "Steam cloud list session prep threw: " << ex.what() << '\n';
        return 1;
    } catch (...) {
        err << "Steam cloud list session prep threw: unknown exception\n";
        return 1;
    }

    SteamCloudFileListResult result;
    try {
        result = list_remote_files(effective_request, count, start_index, extended_details);
    } catch (const std::exception& ex) {
        err << "Steam cloud list fetch threw: " << ex.what() << '\n';
        return 1;
    } catch (...) {
        err << "Steam cloud list fetch threw: unknown exception\n";
        return 1;
    }
    if (!result.ok) {
        err << result.message << '\n';
        return 1;
    }

    const auto remote_prefix = normalize_slashes(request.remote_root);
    std::uint64_t matched_count = 0;
    std::uint64_t skipped_count = 0;
    for (const auto& file : result.files) {
        const auto normalized_name = normalize_slashes(file.filename);
        if (!starts_with_path_prefix(normalized_name, remote_prefix)) {
            ++skipped_count;
            continue;
        }
        ++matched_count;
    }

    out << "Steam Cloud files for app " << result.app_id
        << ": returned=" << result.files.size()
        << " matched=" << matched_count
        << " filtered_out=" << skipped_count
        << " total=" << result.total_files
        << " eresult=" << result.eresult;
    if (!request.remote_root.empty()) {
        out << " remote_root=" << request.remote_root;
    }
    out << '\n';
    for (const auto& file : result.files) {
        const auto normalized_name = normalize_slashes(file.filename);
        if (!starts_with_path_prefix(normalized_name, remote_prefix)) {
            continue;
        }
        out << "  file=" << file.filename
            << " size=" << file.file_size
            << " timestamp=" << file.timestamp;
        if (!file.file_sha.empty()) {
            out << " sha=" << file.file_sha;
        }
        if (!file.platforms_to_sync.empty()) {
            out << " platforms=" << file.platforms_to_sync;
        }
        out << '\n';
    }
    return 0;
}

int run_pull_cloud_save(core::session::SessionRepository& store,
                        const SteamCloudRequest& request,
                        std::ostream& out,
                        std::ostream& err) {
    const auto result = pull_cloud_save(with_saved_auth_session(store, request));
    if (!result.ok) {
        err << result.message << '\n';
    }
    print_sync_result_summary(result, out);
    return result.ok ? 0 : 1;
}

int run_push_cloud_save(core::session::SessionRepository& store,
                        const SteamCloudRequest& request,
                        std::ostream& out,
                        std::ostream& err) {
    const auto result = push_cloud_save(with_saved_auth_session(store, request));
    if (!result.ok) {
        err << result.message << '\n';
    }
    print_sync_result_summary(result, out);
    return result.ok ? 0 : 1;
}

int run_verify_cloud_local(core::session::SessionRepository& store,
                           const SteamCloudRequest& request,
                           bool include_extra_local,
                           std::ostream& out,
                           std::ostream& err) {
    const auto result =
        verify_cloud_local_files(with_saved_auth_session(store, request), include_extra_local);
    for (const auto& entry : result.entries) {
        out << verify_status_name(entry.status)
            << " remote=" << (entry.remote_filename.empty() ? "(none)" : entry.remote_filename)
            << " local=" << (entry.local_path.empty() ? "(none)" : entry.local_path);
        if (entry.remote_size != 0) {
            out << " remote_size=" << entry.remote_size;
        }
        if (entry.local_size != 0) {
            out << " local_size=" << entry.local_size;
        }
        if (!entry.remote_sha.empty()) {
            out << " remote_sha=" << entry.remote_sha;
        }
        if (!entry.local_sha.empty()) {
            out << " local_sha=" << entry.local_sha;
        }
        if (!entry.reason.empty()) {
            out << " reason=" << entry.reason;
        }
        out << '\n';
    }
    if (result.fatal_error) {
        err << result.message << '\n';
    }
    print_verify_result_summary(result, out);
    return result.clean() ? 0 : 1;
}

namespace testing {

void set_list_remote_files_hook(ListRemoteFilesHook hook) { g_list_remote_files_hook = hook; }
void set_download_file_hook(DownloadFileHook hook) { g_download_file_hook = hook; }
void set_upload_cloud_files_hook(UploadCloudFilesHook hook) { g_upload_cloud_files_hook = hook; }
void clear_sync_test_hooks() {
    g_list_remote_files_hook = nullptr;
    g_download_file_hook = nullptr;
    g_upload_cloud_files_hook = nullptr;
}

} // namespace testing

} // namespace cauth::steam::cloud
