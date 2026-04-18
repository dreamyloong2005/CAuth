#include "cauth/core_ffi.h"

#include "core/platform/session_repository_factory.hpp"
#include "core/version.hpp"
#include "core/session/auth_session.hpp"
#include "ffi/client_internal.hpp"

#include <chrono>
#include <memory>
#include <new>
#include <string>

namespace {

std::string nullable_string(const char* value) {
    return value == nullptr ? std::string{} : std::string{value};
}

thread_local std::string g_last_saved_provider;
thread_local std::string g_last_saved_subject_id;
thread_local std::string g_last_saved_account_name;
thread_local std::string g_last_saved_refresh_token;
thread_local std::string g_last_saved_access_token;

} // namespace

cauth_version_t cauth_get_version(void) {
    const auto version = cauth::core::version();
    return cauth_version_t{version.major, version.minor, version.patch, version.text.data()};
}

cauth_result_t cauth_client_create(cauth_client_t** out_client) {
    if (out_client == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        *out_client = new cauth_client{};
        (*out_client)->session_repository =
            cauth::core::platform::make_platform_session_repository();
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
                                       cauth_session_record_t* out_session) {
    if (client == nullptr || out_session == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    out_session->present = 0;
    out_session->provider = "";
    out_session->subject_id = "";
    out_session->account_name = "";
    out_session->refresh_token = "";
    out_session->access_token = "";
    out_session->has_refresh_token = 0;
    out_session->has_access_token = 0;
    out_session->created_at_unix_seconds = 0;

    try {
        const auto session = client->session_repository->load_auth_session();
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
        out_session->created_at_unix_seconds = static_cast<unsigned long long>(
            std::chrono::duration_cast<std::chrono::seconds>(
                session->created_at.time_since_epoch())
                .count());
        return CAUTH_OK;
    } catch (...) {
        return CAUTH_ERROR_INTERNAL;
    }
}

cauth_result_t cauth_session_clear_saved(cauth_client_t* client) {
    if (client == nullptr) {
        return CAUTH_ERROR_INVALID_ARGUMENT;
    }

    try {
        client->session_repository->clear_auth_session();
        g_last_saved_provider.clear();
        g_last_saved_subject_id.clear();
        g_last_saved_account_name.clear();
        g_last_saved_refresh_token.clear();
        g_last_saved_access_token.clear();
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
