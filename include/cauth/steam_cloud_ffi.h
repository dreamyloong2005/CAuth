#ifndef CAUTH_STEAM_CLOUD_FFI_H
#define CAUTH_STEAM_CLOUD_FFI_H

#include "cauth/core_ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cauth_steam_cloud_direction {
    CAUTH_STEAM_CLOUD_PULL = 0,
    CAUTH_STEAM_CLOUD_PUSH = 1
} cauth_steam_cloud_direction_t;

typedef enum cauth_steam_cloud_conflict_policy {
    CAUTH_STEAM_CLOUD_CONFLICT_DEFAULT = 0,
    CAUTH_STEAM_CLOUD_CONFLICT_LOCAL_WINS = 1,
    CAUTH_STEAM_CLOUD_CONFLICT_REMOTE_WINS = 2,
    CAUTH_STEAM_CLOUD_CONFLICT_NEWER_WINS = 3,
    CAUTH_STEAM_CLOUD_CONFLICT_FAIL_ON_CONFLICT = 4
} cauth_steam_cloud_conflict_policy_t;

typedef enum cauth_steam_cloud_backend {
    CAUTH_STEAM_CLOUD_BACKEND_AUTO = 0,
    CAUTH_STEAM_CLOUD_BACKEND_WEB = 1,
    CAUTH_STEAM_CLOUD_BACKEND_CM = 2
} cauth_steam_cloud_backend_t;

typedef struct cauth_steam_cloud_request {
    unsigned int app_id;
    unsigned long long steam_id;
    const char* access_token;
    const char* local_root;
    const char* remote_root;
    int dry_run;
    int delete_remote_orphans;
    cauth_steam_cloud_conflict_policy_t conflict_policy;
    cauth_steam_cloud_backend_t backend;
    cauth_file_write_mode_t local_write_mode;
    int atomic_write;
    cauth_route_selection_t route_selection;
} cauth_steam_cloud_request_t;

typedef struct cauth_steam_cloud_file_entry {
    unsigned int app_id;
    unsigned long long ugc_id;
    const char* filename;
    unsigned long long timestamp;
    unsigned int file_size;
    const char* url;
    unsigned long long steam_id_creator;
    unsigned int flags;
    const char* platforms_to_sync;
    const char* file_sha;
} cauth_steam_cloud_file_entry_t;

typedef struct cauth_steam_cloud_file_list {
    int ok;
    int present;
    unsigned int app_id;
    unsigned int eresult;
    const char* module_status;
    unsigned long long total_files;
    unsigned long long file_count;
    const cauth_steam_cloud_file_entry_t* files;
    const char* message;
} cauth_steam_cloud_file_list_t;

typedef struct cauth_steam_cloud_result {
    int ok;
    unsigned int app_id;
    cauth_steam_cloud_direction_t direction;
    cauth_steam_cloud_conflict_policy_t conflict_policy;
    const char* module_status;
    unsigned long long local_file_count;
    unsigned long long remote_file_count;
    unsigned long long transferred_count;
    unsigned long long deleted_count;
    unsigned long long skipped_count;
    unsigned long long conflict_count;
    unsigned long long transferred_bytes;
    int resumable;
    int resumed;
    unsigned long long resume_from_bytes;
    const char* message;
} cauth_steam_cloud_result_t;

typedef enum cauth_steam_cloud_verify_status {
    CAUTH_STEAM_CLOUD_VERIFY_OK = 0,
    CAUTH_STEAM_CLOUD_VERIFY_MISSING_LOCAL = 1,
    CAUTH_STEAM_CLOUD_VERIFY_MISMATCHED = 2,
    CAUTH_STEAM_CLOUD_VERIFY_SIZE_ONLY = 3,
    CAUTH_STEAM_CLOUD_VERIFY_EXTRA_LOCAL = 4
} cauth_steam_cloud_verify_status_t;

typedef struct cauth_steam_cloud_verify_entry {
    const char* remote_filename;
    const char* local_path;
    cauth_steam_cloud_verify_status_t status;
    unsigned int remote_size;
    unsigned long long remote_timestamp;
    const char* remote_sha;
    unsigned long long local_size;
    const char* local_sha;
    const char* reason;
} cauth_steam_cloud_verify_entry_t;

typedef struct cauth_steam_cloud_verify_report {
    int present;
    int clean;
    int include_extra_local;
    unsigned int app_id;
    const char* module_status;
    unsigned long long checked_count;
    unsigned long long ok_count;
    unsigned long long missing_count;
    unsigned long long mismatched_count;
    unsigned long long size_only_count;
    unsigned long long filtered_out_count;
    unsigned long long extra_local_count;
    unsigned long long total_count;
    unsigned long long entry_count;
    const cauth_steam_cloud_verify_entry_t* entries;
    const char* message;
} cauth_steam_cloud_verify_report_t;

typedef enum cauth_steam_cloud_task_kind {
    CAUTH_STEAM_CLOUD_TASK_PULL = 1,
    CAUTH_STEAM_CLOUD_TASK_PUSH = 2,
    CAUTH_STEAM_CLOUD_TASK_VERIFY = 3
} cauth_steam_cloud_task_kind_t;

typedef enum cauth_steam_cloud_route_task {
    CAUTH_STEAM_CLOUD_ROUTE_LIST = 1,
    CAUTH_STEAM_CLOUD_ROUTE_VERIFY = 2,
    CAUTH_STEAM_CLOUD_ROUTE_PULL = 3,
    CAUTH_STEAM_CLOUD_ROUTE_PUSH = 4
} cauth_steam_cloud_route_task_t;

typedef struct cauth_steam_cloud_task_snapshot {
    unsigned long long handle;
    int active;
    int succeeded;
    int canceled;
    int paused;
    cauth_steam_cloud_task_kind_t kind;
    const char* module_status;
    const char* phase;
    const char* target;
    const char* message;
    unsigned long long completed_steps;
    unsigned long long total_steps;
    unsigned long long completed_bytes;
    unsigned long long total_bytes;
    int resumable;
    int resumed;
    unsigned long long resume_from_bytes;
    int has_result;
    cauth_steam_cloud_result_t result;
    int has_verify_report;
    cauth_steam_cloud_verify_report_t verify_report;
} cauth_steam_cloud_task_snapshot_t;

CAUTH_API cauth_result_t cauth_steam_cloud_list_remote_files(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    unsigned int count,
    unsigned int start_index,
    int extended_details,
    cauth_steam_cloud_file_list_t* out_response);
CAUTH_API cauth_result_t cauth_steam_cloud_list_remote_files_via_web_page(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    unsigned int count,
    unsigned int start_index,
    cauth_steam_cloud_file_list_t* out_response);
CAUTH_API cauth_result_t cauth_steam_cloud_probe_routes(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    cauth_steam_cloud_route_task_t task,
    unsigned int max_count,
    cauth_route_probe_result_t* out_result);
CAUTH_API cauth_result_t cauth_steam_cloud_pull(cauth_client_t* client,
                                               const cauth_steam_cloud_request_t* request,
                                               cauth_steam_cloud_result_t* out_result);
CAUTH_API cauth_result_t cauth_steam_cloud_start_pull(cauth_client_t* client,
                                                      const cauth_steam_cloud_request_t* request,
                                                      unsigned long long* out_handle);
CAUTH_API cauth_result_t cauth_steam_cloud_push(cauth_client_t* client,
                                               const cauth_steam_cloud_request_t* request,
                                               cauth_steam_cloud_result_t* out_result);
CAUTH_API cauth_result_t cauth_steam_cloud_start_push(cauth_client_t* client,
                                                      const cauth_steam_cloud_request_t* request,
                                                      unsigned long long* out_handle);
CAUTH_API cauth_result_t cauth_steam_cloud_verify_local_files(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    int include_extra_local,
    cauth_steam_cloud_verify_report_t* out_result);
CAUTH_API cauth_result_t cauth_steam_cloud_start_verify_local_files(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    int include_extra_local,
    unsigned long long* out_handle);
CAUTH_API cauth_result_t cauth_steam_cloud_poll_task(unsigned long long handle,
                                                     cauth_steam_cloud_task_snapshot_t* out_snapshot);
CAUTH_API void cauth_steam_cloud_pause_task(unsigned long long handle);
CAUTH_API void cauth_steam_cloud_cancel_task(unsigned long long handle);
CAUTH_API void cauth_steam_cloud_dispose_task(unsigned long long handle);

#ifdef __cplusplus
}
#endif

#endif
