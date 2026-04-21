#include "cauth/steam_cloud_ffi.h"

#include "core/session/auth_session.hpp"
#include "ffi/client_internal.hpp"
#include "steam/cloud/steam_cloud_application.hpp"

#include <new>
#include <optional>
#include <string>
#include <vector>

namespace {

std::string nullable_string(const char* value) {
    return value == nullptr ? std::string{} : std::string{value};
}

thread_local std::string g_last_cloud_message;
thread_local std::vector<std::string> g_cloud_filenames;
thread_local std::vector<std::string> g_cloud_urls;
thread_local std::vector<std::string> g_cloud_platforms;
thread_local std::vector<std::string> g_cloud_shas;
thread_local std::vector<cauth_steam_cloud_file_entry_t> g_cloud_entries;

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

        out_result->ok = result.ok ? 1 : 0;
        out_result->app_id = result.app_id;
        out_result->direction = to_ffi_direction(result.direction);
        out_result->conflict_policy = to_ffi_conflict_policy(result.conflict_policy);
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
    out_result->checked_count = 0;
    out_result->ok_count = 0;
    out_result->missing_count = 0;
    out_result->mismatched_count = 0;
    out_result->size_only_count = 0;
    out_result->filtered_out_count = 0;
    out_result->extra_local_count = 0;
    out_result->total_count = 0;
    out_result->message = "";

    try {
        const auto native_request = build_native_request(client, request);
        const auto result = cauth::steam::cloud::verify_cloud_local_files(
            native_request, include_extra_local != 0);
        g_last_cloud_message = result.message;

        out_result->present = 1;
        out_result->clean = result.clean() ? 1 : 0;
        out_result->include_extra_local = result.include_extra_local ? 1 : 0;
        out_result->app_id = result.app_id;
        out_result->checked_count = result.checked_count;
        out_result->ok_count = result.ok_count;
        out_result->missing_count = result.missing_count;
        out_result->mismatched_count = result.mismatched_count;
        out_result->size_only_count = result.size_only_count;
        out_result->filtered_out_count = result.filtered_out_count;
        out_result->extra_local_count = result.extra_local_count;
        out_result->total_count = result.total_count;
        out_result->message = g_last_cloud_message.c_str();
        return CAUTH_OK;
    } catch (const std::bad_alloc&) {
        return CAUTH_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}
