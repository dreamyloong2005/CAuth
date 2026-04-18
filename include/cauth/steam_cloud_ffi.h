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

typedef struct cauth_steam_cloud_request {
    unsigned int app_id;
    const char* access_token;
    const char* local_root;
    const char* remote_root;
    int dry_run;
    int delete_remote_orphans;
    cauth_steam_cloud_conflict_policy_t conflict_policy;
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
    unsigned long long local_file_count;
    unsigned long long remote_file_count;
    unsigned long long transferred_count;
    unsigned long long deleted_count;
    unsigned long long skipped_count;
    unsigned long long conflict_count;
    unsigned long long transferred_bytes;
    const char* message;
} cauth_steam_cloud_result_t;

typedef struct cauth_steam_cloud_verify_report {
    int present;
    int clean;
    int include_extra_local;
    unsigned int app_id;
    unsigned long long checked_count;
    unsigned long long ok_count;
    unsigned long long missing_count;
    unsigned long long mismatched_count;
    unsigned long long size_only_count;
    unsigned long long filtered_out_count;
    unsigned long long extra_local_count;
    unsigned long long total_count;
    const char* message;
} cauth_steam_cloud_verify_report_t;

CAUTH_API cauth_result_t cauth_steam_cloud_list_remote_files(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    unsigned int count,
    unsigned int start_index,
    int extended_details,
    cauth_steam_cloud_file_list_t* out_response);
CAUTH_API cauth_result_t cauth_steam_cloud_pull(cauth_client_t* client,
                                               const cauth_steam_cloud_request_t* request,
                                               cauth_steam_cloud_result_t* out_result);
CAUTH_API cauth_result_t cauth_steam_cloud_push(cauth_client_t* client,
                                               const cauth_steam_cloud_request_t* request,
                                               cauth_steam_cloud_result_t* out_result);
CAUTH_API cauth_result_t cauth_steam_cloud_verify_local_files(
    cauth_client_t* client,
    const cauth_steam_cloud_request_t* request,
    int include_extra_local,
    cauth_steam_cloud_verify_report_t* out_result);

#ifdef __cplusplus
}
#endif

#endif
