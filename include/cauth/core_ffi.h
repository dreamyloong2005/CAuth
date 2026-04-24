#ifndef CAUTH_CORE_FFI_H
#define CAUTH_CORE_FFI_H

#include "cauth/ffi_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cauth_file_write_mode {
    CAUTH_FILE_WRITE_OVERWRITE = 0,
    CAUTH_FILE_WRITE_SKIP_EXISTING = 1,
    CAUTH_FILE_WRITE_FAIL_IF_EXISTS = 2
} cauth_file_write_mode_t;

typedef enum cauth_session_storage_kind {
    CAUTH_SESSION_STORAGE_DEFAULT = 0,
    CAUTH_SESSION_STORAGE_MEMORY = 1,
    CAUTH_SESSION_STORAGE_FILE_PATH = 2,
    CAUTH_SESSION_STORAGE_SECURE_STORAGE = 3
} cauth_session_storage_kind_t;

typedef struct cauth_client_options {
    cauth_session_storage_kind_t session_storage_kind;
    const char* session_storage_path;
    const char* session_storage_namespace;
    const char* session_storage_key;
} cauth_client_options_t;

typedef struct cauth_route_selection {
    const char* endpoint;
    const char* protocol;
    const char* role;
} cauth_route_selection_t;

typedef struct cauth_route_probe_entry {
    const char* endpoint;
    const char* protocol;
    const char* role;
    const char* note;
    unsigned long long latency_ms;
    int latency_known;
    int recent_success;
    int recent_failure;
    unsigned int success_count;
    unsigned int failure_count;
} cauth_route_probe_entry_t;

typedef struct cauth_route_probe_result {
    int ok;
    const char* module_status;
    const char* backend;
    const char* message;
    unsigned long long route_count;
    const cauth_route_probe_entry_t* routes;
} cauth_route_probe_result_t;

CAUTH_API cauth_version_t cauth_get_version(void);
CAUTH_API cauth_result_t cauth_client_create(cauth_client_t** out_client);
CAUTH_API cauth_result_t cauth_client_create_with_options(
    const cauth_client_options_t* options,
    cauth_client_t** out_client);
CAUTH_API void cauth_client_destroy(cauth_client_t* client);
CAUTH_API const char* cauth_result_message(cauth_result_t result);
CAUTH_API cauth_result_t cauth_session_get_saved(cauth_client_t* client,
                                                 const char* provider,
                                                 const char* subject_id,
                                                 cauth_session_record_t* out_session);
CAUTH_API cauth_result_t cauth_session_list_saved(cauth_client_t* client,
                                                  cauth_session_list_t* out_sessions);
CAUTH_API cauth_result_t cauth_session_clear_account(cauth_client_t* client,
                                                     const char* provider,
                                                     const char* subject_id);
CAUTH_API cauth_result_t cauth_session_clear_all(cauth_client_t* client);
CAUTH_API cauth_result_t cauth_session_save(cauth_client_t* client,
                                            const cauth_session_record_t* session);

#ifdef __cplusplus
}
#endif

#endif
