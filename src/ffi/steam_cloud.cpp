#include "cauth/steam_cloud_ffi.h"

#include "core/session/auth_session.hpp"
#include "ffi/client_internal.hpp"
#include "steam/cloud/steam_cloud_application.hpp"

#include <atomic>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::string nullable_string(const char* value) {
    return value == nullptr ? std::string{} : std::string{value};
}

bool message_indicates_cancel(std::string_view message) {
    return message.find("operation canceled") != std::string_view::npos;
}

thread_local std::string g_last_cloud_message;
thread_local std::string g_last_cloud_module_status;
thread_local std::vector<std::string> g_cloud_filenames;
thread_local std::vector<std::string> g_cloud_urls;
thread_local std::vector<std::string> g_cloud_platforms;
thread_local std::vector<std::string> g_cloud_shas;
thread_local std::vector<cauth_steam_cloud_file_entry_t> g_cloud_entries;
thread_local std::string g_cloud_task_phase;
thread_local std::string g_cloud_task_target;
thread_local std::string g_cloud_task_message;
thread_local std::string g_cloud_task_module_status;
thread_local std::string g_cloud_task_result_message;
thread_local std::string g_cloud_task_verify_message;

struct CloudVerifyStorage {
    std::vector<std::string> remote_filenames;
    std::vector<std::string> local_paths;
    std::vector<std::string> remote_shas;
    std::vector<std::string> local_shas;
    std::vector<std::string> reasons;
    std::vector<cauth_steam_cloud_verify_entry_t> entries;
};

thread_local CloudVerifyStorage g_cloud_verify_storage;

struct CloudTask {
    explicit CloudTask(cauth_steam_cloud_task_kind_t task_kind) : kind(task_kind) {}

    std::mutex mutex;
    std::atomic_bool cancel_requested{false};
    std::atomic_bool finished{false};
    cauth_steam_cloud_task_kind_t kind = CAUTH_STEAM_CLOUD_TASK_PULL;
    bool succeeded = false;
    bool canceled = false;
    std::string module_status = "idle";
    std::string phase = "Queued";
    std::string target;
    std::string message;
    std::uint64_t completed_steps = 0;
    std::uint64_t total_steps = 0;
    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
    bool has_result = false;
    cauth_steam_cloud_result_t result{};
    std::string result_message;
    std::string result_module_status;
    bool has_verify_report = false;
    cauth_steam_cloud_verify_report_t verify_report{};
    std::string verify_message;
    std::string verify_module_status;
    CloudVerifyStorage verify_storage;
};

std::mutex g_cloud_tasks_mutex;
std::unordered_map<unsigned long long, std::shared_ptr<CloudTask>> g_cloud_tasks;
std::atomic_ullong g_next_cloud_task_handle{1};

cauth_steam_cloud_direction_t to_ffi_direction(cauth::steam::cloud::SteamCloudDirection direction) {
    switch (direction) {
    case cauth::steam::cloud::SteamCloudDirection::Push:
        return CAUTH_STEAM_CLOUD_PUSH;
    case cauth::steam::cloud::SteamCloudDirection::Pull:
    default:
        return CAUTH_STEAM_CLOUD_PULL;
    }
}

cauth::steam::cloud::SteamCloudConflictPolicy from_ffi_conflict_policy(
    cauth_steam_cloud_conflict_policy_t policy) {
    switch (policy) {
    case CAUTH_STEAM_CLOUD_CONFLICT_LOCAL_WINS:
        return cauth::steam::cloud::SteamCloudConflictPolicy::LocalWins;
    case CAUTH_STEAM_CLOUD_CONFLICT_REMOTE_WINS:
        return cauth::steam::cloud::SteamCloudConflictPolicy::RemoteWins;
    case CAUTH_STEAM_CLOUD_CONFLICT_NEWER_WINS:
        return cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins;
    case CAUTH_STEAM_CLOUD_CONFLICT_FAIL_ON_CONFLICT:
        return cauth::steam::cloud::SteamCloudConflictPolicy::FailOnConflict;
    case CAUTH_STEAM_CLOUD_CONFLICT_DEFAULT:
    default:
        return cauth::steam::cloud::SteamCloudConflictPolicy::Default;
    }
}

cauth::core::platform::FileWriteMode from_ffi_file_write_mode(cauth_file_write_mode_t mode) {
    using WriteMode = cauth::core::platform::FileWriteMode;
    switch (mode) {
    case CAUTH_FILE_WRITE_SKIP_EXISTING:
        return WriteMode::SkipExisting;
    case CAUTH_FILE_WRITE_FAIL_IF_EXISTS:
        return WriteMode::FailIfExists;
    case CAUTH_FILE_WRITE_OVERWRITE:
    default:
        return WriteMode::Overwrite;
    }
}

cauth::steam::cloud::SteamCloudRequest build_native_request(cauth_client_t* client,
                                                            const cauth_steam_cloud_request_t* request) {
    cauth::steam::cloud::SteamCloudRequest native_request;
    native_request.app_id = request->app_id;
    native_request.steam_id = request->steam_id;
    native_request.access_token = nullable_string(request->access_token);
    native_request.local_root = nullable_string(request->local_root);
    native_request.remote_root = nullable_string(request->remote_root);
    native_request.dry_run = request->dry_run != 0;
    native_request.delete_remote_orphans = request->delete_remote_orphans != 0;
    native_request.conflict_policy = from_ffi_conflict_policy(request->conflict_policy);
    native_request.backend = cauth::steam::cloud::SteamCloudBackend::Auto;
    native_request.local_write_options.mode = from_ffi_file_write_mode(request->local_write_mode);
    native_request.local_write_options.atomic_write = request->atomic_write != 0;
    native_request.local_write_options.temp_suffix = ".cauthdownload";
    if (native_request.access_token.empty() || native_request.refresh_token.empty() ||
        native_request.steam_id == 0) {
        const auto session = native_request.steam_id == 0
                                 ? std::nullopt
                                 : client->session_repository->load_auth_session(
                                       "steam",
                                       std::to_string(native_request.steam_id));
        if (session.has_value()) {
            if (native_request.access_token.empty()) {
                native_request.access_token = session->access_token;
            }
            if (native_request.refresh_token.empty()) {
                native_request.refresh_token = session->refresh_token;
            }
            if (native_request.session_type.empty()) {
                native_request.session_type = session->session_type;
            }
        }
    }
    return native_request;
}

cauth_steam_cloud_conflict_policy_t to_ffi_conflict_policy(
    cauth::steam::cloud::SteamCloudConflictPolicy policy) {
    switch (policy) {
    case cauth::steam::cloud::SteamCloudConflictPolicy::LocalWins:
        return CAUTH_STEAM_CLOUD_CONFLICT_LOCAL_WINS;
    case cauth::steam::cloud::SteamCloudConflictPolicy::RemoteWins:
        return CAUTH_STEAM_CLOUD_CONFLICT_REMOTE_WINS;
    case cauth::steam::cloud::SteamCloudConflictPolicy::NewerWins:
        return CAUTH_STEAM_CLOUD_CONFLICT_NEWER_WINS;
    case cauth::steam::cloud::SteamCloudConflictPolicy::FailOnConflict:
        return CAUTH_STEAM_CLOUD_CONFLICT_FAIL_ON_CONFLICT;
    case cauth::steam::cloud::SteamCloudConflictPolicy::Default:
    default:
        return CAUTH_STEAM_CLOUD_CONFLICT_DEFAULT;
    }
}

bool cloud_task_cancel_requested(void* user_data) {
    const auto* task = static_cast<const CloudTask*>(user_data);
    return task != nullptr && task->cancel_requested.load();
}

void on_cloud_task_progress(const cauth::steam::cloud::SteamCloudTransferProgress& progress,
                            void* user_data) {
    auto* task = static_cast<CloudTask*>(user_data);
    if (task == nullptr) {
        return;
    }
    std::lock_guard lock{task->mutex};
    task->module_status = progress.module_status.empty() ? "idle" : progress.module_status;
    task->phase = progress.phase;
    task->target = progress.target;
    task->completed_steps = progress.completed_steps;
    task->total_steps = progress.total_steps;
    task->completed_bytes = progress.completed_bytes;
    task->total_bytes = progress.total_bytes;
}

std::shared_ptr<CloudTask> find_cloud_task(unsigned long long handle) {
    std::lock_guard lock{g_cloud_tasks_mutex};
    const auto found = g_cloud_tasks.find(handle);
    if (found == g_cloud_tasks.end()) {
        return nullptr;
    }
    return found->second;
}

void fill_cloud_result(const cauth::steam::cloud::SteamCloudResult& source,
                       cauth_steam_cloud_result_t& destination,
                       std::string& message_storage,
                       std::string& module_status_storage) {
    message_storage = source.message;
    module_status_storage = source.module_status;
    destination.ok = source.ok ? 1 : 0;
    destination.app_id = source.app_id;
    destination.direction = to_ffi_direction(source.direction);
    destination.conflict_policy = to_ffi_conflict_policy(source.conflict_policy);
    destination.local_file_count = source.local_file_count;
    destination.remote_file_count = source.remote_file_count;
    destination.transferred_count = source.transferred_count;
    destination.deleted_count = source.deleted_count;
    destination.skipped_count = source.skipped_count;
    destination.conflict_count = source.conflict_count;
    destination.transferred_bytes = source.transferred_bytes;
    destination.module_status = module_status_storage.c_str();
    destination.message = message_storage.c_str();
}

void fill_cloud_verify_report(const cauth::steam::cloud::SteamCloudVerifyResult& source,
                              cauth_steam_cloud_verify_report_t& destination,
                              std::string& message_storage,
                              std::string& module_status_storage,
                              CloudVerifyStorage& storage) {
    message_storage = source.message;
    module_status_storage = source.module_status;
    storage.remote_filenames.clear();
    storage.local_paths.clear();
    storage.remote_shas.clear();
    storage.local_shas.clear();
    storage.reasons.clear();
    storage.entries.clear();
    storage.remote_filenames.reserve(source.entries.size());
    storage.local_paths.reserve(source.entries.size());
    storage.remote_shas.reserve(source.entries.size());
    storage.local_shas.reserve(source.entries.size());
    storage.reasons.reserve(source.entries.size());
    for (const auto& entry : source.entries) {
        storage.remote_filenames.push_back(entry.remote_filename);
        storage.local_paths.push_back(entry.local_path);
        storage.remote_shas.push_back(entry.remote_sha);
        storage.local_shas.push_back(entry.local_sha);
        storage.reasons.push_back(entry.reason);
    }
    storage.entries.reserve(source.entries.size());
    for (std::size_t index = 0; index < source.entries.size(); ++index) {
        const auto& entry = source.entries[index];
        cauth_steam_cloud_verify_status_t status = CAUTH_STEAM_CLOUD_VERIFY_OK;
        switch (entry.status) {
        case cauth::steam::cloud::SteamCloudVerifyStatus::MissingLocal:
            status = CAUTH_STEAM_CLOUD_VERIFY_MISSING_LOCAL;
            break;
        case cauth::steam::cloud::SteamCloudVerifyStatus::Mismatched:
            status = CAUTH_STEAM_CLOUD_VERIFY_MISMATCHED;
            break;
        case cauth::steam::cloud::SteamCloudVerifyStatus::SizeOnly:
            status = CAUTH_STEAM_CLOUD_VERIFY_SIZE_ONLY;
            break;
        case cauth::steam::cloud::SteamCloudVerifyStatus::ExtraLocal:
            status = CAUTH_STEAM_CLOUD_VERIFY_EXTRA_LOCAL;
            break;
        case cauth::steam::cloud::SteamCloudVerifyStatus::Ok:
        default:
            status = CAUTH_STEAM_CLOUD_VERIFY_OK;
            break;
        }
        storage.entries.push_back(cauth_steam_cloud_verify_entry_t{
            storage.remote_filenames[index].c_str(),
            storage.local_paths[index].c_str(),
            status,
            entry.remote_size,
            entry.remote_timestamp,
            storage.remote_shas[index].c_str(),
            entry.local_size,
            storage.local_shas[index].c_str(),
            storage.reasons[index].c_str(),
        });
    }
    destination.present = 1;
    destination.clean = source.clean() ? 1 : 0;
    destination.include_extra_local = source.include_extra_local ? 1 : 0;
    destination.app_id = source.app_id;
    destination.checked_count = source.checked_count;
    destination.ok_count = source.ok_count;
    destination.missing_count = source.missing_count;
    destination.mismatched_count = source.mismatched_count;
    destination.size_only_count = source.size_only_count;
    destination.filtered_out_count = source.filtered_out_count;
    destination.extra_local_count = source.extra_local_count;
    destination.total_count = source.total_count;
    destination.entry_count = static_cast<unsigned long long>(storage.entries.size());
    destination.entries = storage.entries.empty() ? nullptr : storage.entries.data();
    destination.module_status = module_status_storage.c_str();
    destination.message = message_storage.c_str();
}

template <typename Runner>
cauth_result_t start_cloud_task(cauth_steam_cloud_task_kind_t kind,
                                Runner&& runner,
                                unsigned long long* out_handle) {
    if (out_handle == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    const auto handle = g_next_cloud_task_handle.fetch_add(1);
    auto task = std::make_shared<CloudTask>(kind);
    {
        std::lock_guard lock{g_cloud_tasks_mutex};
        g_cloud_tasks.emplace(handle, task);
    }

    std::thread([task, runner = std::forward<Runner>(runner)]() mutable {
        cauth::steam::cloud::set_current_thread_steam_cloud_transfer_hooks(
            &on_cloud_task_progress,
            &cloud_task_cancel_requested,
            task.get());
        try {
            runner(*task);
        } catch (const std::exception& exception) {
            std::lock_guard lock{task->mutex};
            task->succeeded = false;
            task->canceled = false;
            task->module_status = "failed";
            task->message = exception.what();
        } catch (...) {
            std::lock_guard lock{task->mutex};
            task->succeeded = false;
            task->canceled = false;
            task->module_status = "failed";
            task->message = "unexpected exception";
        }
        cauth::steam::cloud::clear_current_thread_steam_cloud_transfer_hooks();
        task->finished.store(true);
    }).detach();

    *out_handle = handle;
    return CAUTH_OK;
}

cauth_result_t run_cloud_operation(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    cauth_steam_cloud_result_t* out_result,
    cauth::steam::cloud::SteamCloudResult (*fn)(const cauth::steam::cloud::SteamCloudRequest&)) {
    if (client == nullptr || request == nullptr || out_result == nullptr ||
        request->app_id == 0 || request->steam_id == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_result->ok = 0;
    out_result->app_id = 0;
    out_result->direction = CAUTH_STEAM_CLOUD_PULL;
    out_result->conflict_policy = CAUTH_STEAM_CLOUD_CONFLICT_DEFAULT;
    out_result->module_status = "";
    out_result->local_file_count = 0;
    out_result->remote_file_count = 0;
    out_result->transferred_count = 0;
    out_result->deleted_count = 0;
    out_result->skipped_count = 0;
    out_result->conflict_count = 0;
    out_result->transferred_bytes = 0;
    out_result->message = "";

    try {
        const auto native_request = build_native_request(client, request);
        const auto result = fn(native_request);
        g_last_cloud_message = result.message;
        g_last_cloud_module_status = result.module_status;

        out_result->ok = result.ok ? 1 : 0;
        out_result->app_id = result.app_id;
        out_result->direction = to_ffi_direction(result.direction);
        out_result->conflict_policy = to_ffi_conflict_policy(result.conflict_policy);
        out_result->module_status = g_last_cloud_module_status.c_str();
        out_result->local_file_count = result.local_file_count;
        out_result->remote_file_count = result.remote_file_count;
        out_result->transferred_count = result.transferred_count;
        out_result->deleted_count = result.deleted_count;
        out_result->skipped_count = result.skipped_count;
        out_result->conflict_count = result.conflict_count;
        out_result->transferred_bytes = result.transferred_bytes;
        out_result->message = g_last_cloud_message.c_str();
        return CAUTH_OK;
    } catch (const std::bad_alloc&) {
        return CAUTH_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

} // namespace

cauth_result_t cauth_steam_cloud_list_remote_files(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    unsigned int count,
    unsigned int start_index,
    int extended_details,
    cauth_steam_cloud_file_list_t* out_response) {
    if (client == nullptr || request == nullptr || out_response == nullptr ||
        request->app_id == 0 || request->steam_id == 0 || count == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_response->ok = 0;
    out_response->present = 0;
    out_response->app_id = 0;
    out_response->eresult = 0;
    out_response->module_status = "";
    out_response->total_files = 0;
    out_response->file_count = 0;
    out_response->files = nullptr;
    out_response->message = "";

    g_last_cloud_message.clear();
    g_cloud_filenames.clear();
    g_cloud_urls.clear();
    g_cloud_platforms.clear();
    g_cloud_shas.clear();
    g_cloud_entries.clear();

    try {
        const auto native_request = build_native_request(client, request);
        const auto result = cauth::steam::cloud::list_remote_files(
            native_request, count, start_index, extended_details != 0);
        g_last_cloud_message = result.message;
        g_last_cloud_module_status = result.module_status;

        g_cloud_filenames.reserve(result.files.size());
        g_cloud_urls.reserve(result.files.size());
        g_cloud_platforms.reserve(result.files.size());
        g_cloud_shas.reserve(result.files.size());
        for (const auto& file : result.files) {
            g_cloud_filenames.push_back(file.filename);
            g_cloud_urls.push_back(file.url);
            g_cloud_platforms.push_back(file.platforms_to_sync);
            g_cloud_shas.push_back(file.file_sha);
        }

        g_cloud_entries.reserve(result.files.size());
        for (std::size_t index = 0; index < result.files.size(); ++index) {
            const auto& file = result.files[index];
            g_cloud_entries.push_back(cauth_steam_cloud_file_entry_t{
                file.app_id,
                file.ugc_id,
                g_cloud_filenames[index].c_str(),
                file.timestamp,
                file.file_size,
                g_cloud_urls[index].c_str(),
                file.steam_id_creator,
                file.flags,
                g_cloud_platforms[index].c_str(),
                g_cloud_shas[index].c_str(),
            });
        }

        out_response->ok = result.ok ? 1 : 0;
        out_response->present = 1;
        out_response->app_id = result.app_id;
        out_response->eresult = result.eresult;
        out_response->module_status = g_last_cloud_module_status.c_str();
        out_response->total_files = result.total_files;
        out_response->file_count = static_cast<unsigned long long>(g_cloud_entries.size());
        out_response->files = g_cloud_entries.empty() ? nullptr : g_cloud_entries.data();
        out_response->message = g_last_cloud_message.c_str();
        return CAUTH_OK;
    } catch (const std::bad_alloc&) {
        return CAUTH_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_steam_cloud_pull(cauth_client_t* client,
                                     const cauth_steam_cloud_request_t* request,
                                     cauth_steam_cloud_result_t* out_result) {
    return run_cloud_operation(client, request, out_result, &cauth::steam::cloud::pull_cloud_save);
}

cauth_result_t cauth_steam_cloud_push(cauth_client_t* client,
                                     const cauth_steam_cloud_request_t* request,
                                     cauth_steam_cloud_result_t* out_result) {
    return run_cloud_operation(client, request, out_result, &cauth::steam::cloud::push_cloud_save);
}

cauth_result_t cauth_steam_cloud_verify_local_files(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    int include_extra_local,
    cauth_steam_cloud_verify_report_t* out_result) {
    if (client == nullptr || request == nullptr || out_result == nullptr ||
        request->app_id == 0 || request->steam_id == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_result->present = 0;
    out_result->clean = 0;
    out_result->include_extra_local = include_extra_local != 0 ? 1 : 0;
    out_result->app_id = 0;
    out_result->module_status = "";
    out_result->checked_count = 0;
    out_result->ok_count = 0;
    out_result->missing_count = 0;
    out_result->mismatched_count = 0;
    out_result->size_only_count = 0;
    out_result->filtered_out_count = 0;
    out_result->extra_local_count = 0;
    out_result->total_count = 0;
    out_result->entry_count = 0;
    out_result->entries = nullptr;
    out_result->message = "";
    g_cloud_verify_storage = CloudVerifyStorage{};

    try {
        const auto native_request = build_native_request(client, request);
        const auto result = cauth::steam::cloud::verify_cloud_local_files(
            native_request, include_extra_local != 0);
        g_last_cloud_message = result.message;
        g_last_cloud_module_status = result.module_status;

        fill_cloud_verify_report(
            result,
            *out_result,
            g_last_cloud_message,
            g_last_cloud_module_status,
            g_cloud_verify_storage);
        return CAUTH_OK;
    } catch (const std::bad_alloc&) {
        return CAUTH_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_steam_cloud_start_pull(cauth_client_t* client,
                                            const cauth_steam_cloud_request_t* request,
                                            unsigned long long* out_handle) {
    if (client == nullptr || request == nullptr || request->app_id == 0 || request->steam_id == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }
    try {
        const auto native_request = build_native_request(client, request);
        return start_cloud_task(
            CAUTH_STEAM_CLOUD_TASK_PULL,
            [native_request](CloudTask& task) {
                const auto result = cauth::steam::cloud::pull_cloud_save(native_request);
                std::lock_guard lock{task.mutex};
                task.has_result = true;
                fill_cloud_result(result, task.result, task.result_message, task.result_module_status);
                task.succeeded = result.ok;
                task.canceled = !result.ok && message_indicates_cancel(result.message);
                task.module_status = result.module_status;
                task.message = result.message.empty() ? (result.ok ? "pull complete" : "pull failed")
                                                     : result.message;
                if (task.module_status == "idle") {
                    task.module_status = task.canceled ? "canceled" : (task.succeeded ? "succeeded" : "failed");
                }
            },
            out_handle);
    } catch (const std::bad_alloc&) {
        return CAUTH_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_steam_cloud_start_push(cauth_client_t* client,
                                            const cauth_steam_cloud_request_t* request,
                                            unsigned long long* out_handle) {
    if (client == nullptr || request == nullptr || request->app_id == 0 || request->steam_id == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }
    try {
        const auto native_request = build_native_request(client, request);
        return start_cloud_task(
            CAUTH_STEAM_CLOUD_TASK_PUSH,
            [native_request](CloudTask& task) {
                const auto result = cauth::steam::cloud::push_cloud_save(native_request);
                std::lock_guard lock{task.mutex};
                task.has_result = true;
                fill_cloud_result(result, task.result, task.result_message, task.result_module_status);
                task.succeeded = result.ok;
                task.canceled = !result.ok && message_indicates_cancel(result.message);
                task.module_status = result.module_status;
                task.message = result.message.empty() ? (result.ok ? "push complete" : "push failed")
                                                     : result.message;
                if (task.module_status == "idle") {
                    task.module_status = task.canceled ? "canceled" : (task.succeeded ? "succeeded" : "failed");
                }
            },
            out_handle);
    } catch (const std::bad_alloc&) {
        return CAUTH_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_steam_cloud_start_verify_local_files(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    int include_extra_local,
    unsigned long long* out_handle) {
    if (client == nullptr || request == nullptr || request->app_id == 0 || request->steam_id == 0) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }
    try {
        const auto native_request = build_native_request(client, request);
        return start_cloud_task(
            CAUTH_STEAM_CLOUD_TASK_VERIFY,
            [native_request, include_extra_local](CloudTask& task) {
                const auto result = cauth::steam::cloud::verify_cloud_local_files(
                    native_request,
                    include_extra_local != 0);
                std::lock_guard lock{task.mutex};
                task.has_verify_report = true;
                fill_cloud_verify_report(
                    result,
                    task.verify_report,
                    task.verify_message,
                    task.verify_module_status,
                    task.verify_storage);
                task.succeeded = result.clean();
                task.canceled = !task.succeeded && message_indicates_cancel(result.message);
                task.module_status = result.module_status;
                task.message = result.message.empty() ? (task.succeeded ? "verify complete" : "verify failed")
                                                     : result.message;
                if (task.module_status == "idle") {
                    task.module_status = task.canceled ? "canceled" : (task.succeeded ? "succeeded" : "failed");
                }
            },
            out_handle);
    } catch (const std::bad_alloc&) {
        return CAUTH_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_steam_cloud_poll_task(unsigned long long handle,
                                           cauth_steam_cloud_task_snapshot_t* out_snapshot) {
    if (out_snapshot == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }
    const auto task = find_cloud_task(handle);
    if (task == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    std::lock_guard lock{task->mutex};
    g_cloud_task_module_status = task->module_status;
    g_cloud_task_phase = task->phase;
    g_cloud_task_target = task->target;
    g_cloud_task_message = task->message;
    g_cloud_task_result_message = task->result_message;
    g_cloud_task_verify_message = task->verify_message;

    out_snapshot->handle = handle;
    out_snapshot->active = task->finished.load() ? 0 : 1;
    out_snapshot->succeeded = task->succeeded ? 1 : 0;
    out_snapshot->canceled = task->canceled ? 1 : 0;
    out_snapshot->kind = task->kind;
    out_snapshot->module_status = g_cloud_task_module_status.c_str();
    out_snapshot->phase = g_cloud_task_phase.c_str();
    out_snapshot->target = g_cloud_task_target.c_str();
    out_snapshot->message = g_cloud_task_message.c_str();
    out_snapshot->completed_steps = task->completed_steps;
    out_snapshot->total_steps = task->total_steps;
    out_snapshot->completed_bytes = task->completed_bytes;
    out_snapshot->total_bytes = task->total_bytes;
    out_snapshot->has_result = task->has_result ? 1 : 0;
    out_snapshot->result = task->result;
    if (out_snapshot->has_result != 0) {
        out_snapshot->result.message = g_cloud_task_result_message.c_str();
    }
    out_snapshot->has_verify_report = task->has_verify_report ? 1 : 0;
    out_snapshot->verify_report = task->verify_report;
    if (out_snapshot->has_verify_report != 0) {
        out_snapshot->verify_report.message = g_cloud_task_verify_message.c_str();
    }
    return CAUTH_OK;
}

void cauth_steam_cloud_cancel_task(unsigned long long handle) {
    const auto task = find_cloud_task(handle);
    if (task == nullptr) {
        return;
    }
    task->cancel_requested.store(true);
    std::lock_guard lock{task->mutex};
    task->module_status = "canceled";
    task->phase = "Cancel requested";
    if (task->message.empty()) {
        task->message = "Cancel requested...";
    }
}

void cauth_steam_cloud_dispose_task(unsigned long long handle) {
    std::lock_guard lock{g_cloud_tasks_mutex};
    g_cloud_tasks.erase(handle);
}
