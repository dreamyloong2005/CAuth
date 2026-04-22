#include "cauth/core_ffi.h"

#include "core/platform/session_repository_factory.hpp"
#include "core/version.hpp"
#include "core/session/auth_session.hpp"
#include "ffi/client_internal.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

std::string nullable_string(const char* value) {
    return value == nullptr ? std::string{} : std::string{value};
}

cauth::core::platform::SessionRepositoryBackend from_ffi_session_storage_kind(
    cauth_session_storage_kind_t kind) {
    using Backend = cauth::core::platform::SessionRepositoryBackend;
    switch (kind) {
    case CAUTH_SESSION_STORAGE_MEMORY:
        return Backend::Memory;
    case CAUTH_SESSION_STORAGE_FILE_PATH:
        return Backend::File;
    case CAUTH_SESSION_STORAGE_SECURE_STORAGE:
        return Backend::SecureStorage;
    case CAUTH_SESSION_STORAGE_DEFAULT:
    default:
        return Backend::Default;
    }
}

cauth::core::platform::SessionRepositoryOptions make_repository_options(
    const cauth_client_options_t* options) {
    cauth::core::platform::SessionRepositoryOptions native_options;
    if (options == nullptr) {
        return native_options;
    }
    native_options.backend = from_ffi_session_storage_kind(options->session_storage_kind);
    native_options.storage_path = nullable_string(options->session_storage_path);
    native_options.storage_namespace = nullable_string(options->session_storage_namespace);
    native_options.storage_key = nullable_string(options->session_storage_key);
    return native_options;
}

thread_local std::string g_last_saved_provider;
thread_local std::string g_last_saved_subject_id;
thread_local std::string g_last_saved_account_name;
thread_local std::string g_last_saved_refresh_token;
thread_local std::string g_last_saved_access_token;

struct SessionRecordStorage {
    std::string provider;
    std::string subject_id;
    std::string account_name;
    std::string refresh_token;
    std::string access_token;
};

thread_local std::vector<SessionRecordStorage> g_last_session_list_storage;
thread_local std::vector<cauth_session_record_t> g_last_session_list_records;

void clear_session_record(cauth_session_record_t& out_session) {
    out_session.present = 0;
    out_session.provider = "";
    out_session.subject_id = "";
    out_session.account_name = "";
    out_session.refresh_token = "";
    out_session.access_token = "";
    out_session.has_refresh_token = 0;
    out_session.has_access_token = 0;
    out_session.created_at_unix_seconds = 0;
}

unsigned long long created_at_seconds(const cauth::core::session::AuthSession& session) {
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            session.created_at.time_since_epoch())
            .count());
}

std::vector<cauth::core::session::AuthSession> account_representatives(
    const std::vector<cauth::core::session::AuthSession>& sessions) {
    std::vector<cauth::core::session::AuthSession> representatives;
    for (const auto& session : sessions) {
        const auto found = std::find_if(
            representatives.begin(),
            representatives.end(),
            [&](const cauth::core::session::AuthSession& candidate) {
                return cauth::core::session::matches_session(candidate,
                                                             session.provider,
                                                             session.subject_id);
            });
        if (found == representatives.end()) {
            representatives.push_back(session);
            continue;
        }
        if (session.created_at >= found->created_at) {
            *found = session;
        }
    }
    return representatives;
}

} // namespace

cauth_version_t cauth_get_version(void) {
    const auto version = cauth::core::version();
    return cauth_version_t{version.major, version.minor, version.patch, version.text.data()};
}

cauth_result_t cauth_client_create(cauth_client_t** out_client) {
    return cauth_client_create_with_options(nullptr, out_client);
}

cauth_result_t cauth_client_create_with_options(const cauth_client_options_t* options,
                                                cauth_client_t** out_client) {
    if (out_client == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        *out_client = new cauth_client{};
        (*out_client)->session_repository = cauth::core::platform::make_platform_session_repository(
            make_repository_options(options));
        return CAUTH_OK;
    } catch (const std::bad_alloc&) {
        *out_client = nullptr;
        return CAUTH_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        *out_client = nullptr;
        return CAUTH_ERROR_INTERNAL;
    }
}

void cauth_client_destroy(cauth_client_t* client) {
    delete client;
}

const char* cauth_result_message(cauth_result_t result) {
    switch (result) {
    case CAUTH_OK:
        return "ok";
    case CAUTH_ERROR_INVALID_ARGUMENT:
        return "invalid argument";
    case CAUTH_ERROR_OUT_OF_MEMORY:
        return "out of memory";
    case CAUTH_ERROR_INTERNAL:
        return "internal error";
    default:
        return "unknown error";
    }
}

cauth_result_t cauth_session_get_saved(cauth_client_t* client,
                                       const char* provider,
                                       const char* subject_id,
                                       cauth_session_record_t* out_session) {
    if (client == nullptr || provider == nullptr || subject_id == nullptr ||
        out_session == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    clear_session_record(*out_session);

    try {
        const auto session = client->session_repository->load_auth_session(provider, subject_id);
        if (!session.has_value()) {
            g_last_saved_provider.clear();
            g_last_saved_subject_id.clear();
            g_last_saved_account_name.clear();
            g_last_saved_refresh_token.clear();
            g_last_saved_access_token.clear();
            return CAUTH_OK;
        }

        g_last_saved_provider = session->provider;
        g_last_saved_subject_id = session->subject_id;
        g_last_saved_account_name = session->account_name;
        g_last_saved_refresh_token = session->refresh_token;
        g_last_saved_access_token = session->access_token;

        out_session->present = 1;
        out_session->provider = g_last_saved_provider.c_str();
        out_session->subject_id = g_last_saved_subject_id.c_str();
        out_session->account_name = g_last_saved_account_name.c_str();
        out_session->refresh_token = g_last_saved_refresh_token.c_str();
        out_session->access_token = g_last_saved_access_token.c_str();
        out_session->has_refresh_token = session->refresh_token.empty() ? 0 : 1;
        out_session->has_access_token = session->access_token.empty() ? 0 : 1;
        out_session->created_at_unix_seconds = created_at_seconds(*session);
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_session_list_saved(cauth_client_t* client,
                                        cauth_session_list_t* out_sessions) {
    if (client == nullptr || out_sessions == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_sessions->sessions = nullptr;
    out_sessions->count = 0;

    try {
        const auto sessions =
            account_representatives(client->session_repository->list_auth_sessions());

        g_last_session_list_storage.clear();
        g_last_session_list_records.clear();
        g_last_session_list_storage.reserve(sessions.size());
        g_last_session_list_records.reserve(sessions.size());

        for (const auto& session : sessions) {
            auto& storage = g_last_session_list_storage.emplace_back();
            storage.provider = session.provider;
            storage.subject_id = session.subject_id;
            storage.account_name = session.account_name;
            storage.refresh_token = session.refresh_token;
            storage.access_token = session.access_token;

            cauth_session_record_t record{};
            record.present = 1;
            record.provider = storage.provider.c_str();
            record.subject_id = storage.subject_id.c_str();
            record.account_name = storage.account_name.c_str();
            record.refresh_token = storage.refresh_token.c_str();
            record.access_token = storage.access_token.c_str();
            record.has_refresh_token = session.refresh_token.empty() ? 0 : 1;
            record.has_access_token = session.access_token.empty() ? 0 : 1;
            record.created_at_unix_seconds = created_at_seconds(session);

            g_last_session_list_records.push_back(record);
        }

        out_sessions->sessions = g_last_session_list_records.data();
        out_sessions->count =
            static_cast<unsigned long long>(g_last_session_list_records.size());
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_session_clear_account(cauth_client_t* client,
                                           const char* provider,
                                           const char* subject_id) {
    if (client == nullptr || provider == nullptr || subject_id == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        client->session_repository->clear_auth_session(provider, subject_id);
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_session_clear_all(cauth_client_t* client) {
    if (client == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        client->session_repository->clear_all_auth_sessions();
        g_last_saved_provider.clear();
        g_last_saved_subject_id.clear();
        g_last_saved_account_name.clear();
        g_last_saved_refresh_token.clear();
        g_last_saved_access_token.clear();
        g_last_session_list_storage.clear();
        g_last_session_list_records.clear();
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_session_save(cauth_client_t* client,
                                  const cauth_session_record_t* session) {
    if (client == nullptr || session == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        cauth::core::session::AuthSession native_session;
        native_session.provider = nullable_string(session->provider);
        native_session.subject_id = nullable_string(session->subject_id);
        native_session.account_name = nullable_string(session->account_name);
        native_session.refresh_token = nullable_string(session->refresh_token);
        native_session.access_token = nullable_string(session->access_token);
        client->session_repository->save_auth_session(native_session);
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}
