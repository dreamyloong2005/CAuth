#ifndef CAUTH_STEAM_DEPOT_FFI_H
#define CAUTH_STEAM_DEPOT_FFI_H

#include "cauth/core_ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cauth_depot_key_response {
    int present;
    int eresult;
    unsigned int depot_id;
    const char* key_hex;
} cauth_depot_key_response_t;

typedef struct cauth_app_branch_entry {
    const char* name;
    const char* build_id;
    const char* description;
    unsigned int time_updated;
    int password_required;
} cauth_app_branch_entry_t;

typedef struct cauth_app_branch_list {
    int present;
    unsigned int app_id;
    unsigned long long branch_count;
    const cauth_app_branch_entry_t* branches;
} cauth_app_branch_list_t;

typedef struct cauth_depot_manifest_entry {
    unsigned int depot_id;
    unsigned long long manifest_gid;
    unsigned long long size;
    unsigned long long download_size;
    int encrypted;
    const char* platform_label;
    const char* os_list;
    const char* os_arch;
    const char* depot_from_app;
    int shared_install;
} cauth_depot_manifest_entry_t;

typedef struct cauth_depot_manifest_list {
    int present;
    unsigned int app_id;
    const char* branch;
    unsigned long long manifest_count;
    const cauth_depot_manifest_entry_t* manifests;
} cauth_depot_manifest_list_t;

typedef struct cauth_depot_preflight_entry {
    unsigned int depot_id;
    unsigned long long manifest_gid;
    unsigned long long size;
    unsigned long long download_size;
    int encrypted;
    const char* platform_label;
    const char* os_list;
    const char* os_arch;
    const char* depot_from_app;
    int shared_install;
    const char* access_status;
    int key_eresult;
    int key_available;
} cauth_depot_preflight_entry_t;

typedef struct cauth_depot_preflight_report {
    int present;
    unsigned int app_id;
    const char* branch;
    const char* build_id;
    unsigned long long depot_count;
    const cauth_depot_preflight_entry_t* depots;
} cauth_depot_preflight_report_t;

typedef struct cauth_manifest_info {
    int present;
    unsigned int depot_id;
    unsigned long long manifest_gid;
    unsigned int creation_time;
    int filenames_encrypted;
    unsigned long long file_count;
    unsigned long long chunk_count;
    unsigned long long total_uncompressed_size;
    unsigned long long total_compressed_size;
    unsigned int unique_chunks;
} cauth_manifest_info_t;

typedef struct cauth_manifest_file_entry {
    const char* filename;
    unsigned int flags;
    unsigned long long size;
    unsigned long long chunk_count;
} cauth_manifest_file_entry_t;

typedef struct cauth_manifest_file_list {
    int present;
    unsigned long long matched_count;
    unsigned long long printed_count;
    unsigned long long total_count;
    const cauth_manifest_file_entry_t* files;
} cauth_manifest_file_list_t;

typedef struct cauth_manifest_request_code_response {
    int present;
    unsigned long long manifest_request_code;
} cauth_manifest_request_code_response_t;

typedef struct cauth_depot_local_verify_report {
    int present;
    int clean;
    const char* module_status;
    unsigned long long checked_count;
    unsigned long long ok_count;
    unsigned long long missing_count;
    unsigned long long mismatched_count;
    unsigned long long size_only_count;
    unsigned long long filtered_out_count;
    unsigned long long total_count;
    unsigned long long entry_count;
    const struct cauth_depot_local_verify_entry* entries;
} cauth_depot_local_verify_report_t;

typedef enum cauth_depot_local_verify_status {
    CAUTH_DEPOT_LOCAL_VERIFY_OK = 0,
    CAUTH_DEPOT_LOCAL_VERIFY_MISSING_LOCAL = 1,
    CAUTH_DEPOT_LOCAL_VERIFY_MISMATCHED = 2,
    CAUTH_DEPOT_LOCAL_VERIFY_SIZE_ONLY = 3,
    CAUTH_DEPOT_LOCAL_VERIFY_FILTERED_OUT = 4
} cauth_depot_local_verify_status_t;

typedef struct cauth_depot_local_verify_entry {
    const char* manifest_filename;
    const char* local_path;
    cauth_depot_local_verify_status_t status;
    unsigned long long expected_size;
    unsigned long long actual_size;
    const char* expected_sha_hex;
    const char* actual_sha_hex;
    const char* reason;
} cauth_depot_local_verify_entry_t;

typedef enum cauth_depot_task_kind {
    CAUTH_DEPOT_TASK_MANIFEST_DOWNLOAD = 1,
    CAUTH_DEPOT_TASK_CHUNK_DOWNLOAD = 2,
    CAUTH_DEPOT_TASK_FILE_DOWNLOAD = 3,
    CAUTH_DEPOT_TASK_ALL_FILES_DOWNLOAD = 4,
    CAUTH_DEPOT_TASK_VERIFY_LOCAL = 5
} cauth_depot_task_kind_t;

typedef struct cauth_depot_task_snapshot {
    unsigned long long handle;
    int active;
    int succeeded;
    int canceled;
    cauth_depot_task_kind_t kind;
    const char* module_status;
    const char* phase;
    const char* target;
    const char* message;
    unsigned long long completed_steps;
    unsigned long long total_steps;
    unsigned long long completed_bytes;
    unsigned long long total_bytes;
    int has_verify_report;
    cauth_depot_local_verify_report_t verify_report;
} cauth_depot_task_snapshot_t;

CAUTH_API cauth_result_t cauth_depot_fetch_branches(cauth_client_t* client,
                                                    unsigned long long steam_id,
                                                    unsigned int app_id,
                                                    unsigned int max_count,
                                                    cauth_app_branch_list_t* out_response);
CAUTH_API cauth_result_t cauth_depot_fetch_manifests(cauth_client_t* client,
                                                     unsigned long long steam_id,
                                                     unsigned int app_id,
                                                     const char* branch,
                                                     unsigned int max_count,
                                                     cauth_depot_manifest_list_t* out_response);
CAUTH_API cauth_result_t cauth_depot_fetch_preflight(cauth_client_t* client,
                                                     unsigned long long steam_id,
                                                     unsigned int app_id,
                                                     const char* branch,
                                                     unsigned int max_count,
                                                     cauth_depot_preflight_report_t* out_response);
CAUTH_API cauth_result_t cauth_depot_download_manifest(unsigned int depot_id,
                                                       unsigned long long manifest_gid,
                                                       unsigned long long request_code,
                                                       unsigned int max_count,
                                                       const char* output_path,
                                                       cauth_file_write_mode_t write_mode,
                                                       int atomic_write);
CAUTH_API cauth_result_t cauth_depot_start_manifest_download(unsigned int depot_id,
                                                             unsigned long long manifest_gid,
                                                             unsigned long long request_code,
                                                             unsigned int max_count,
                                                             const char* output_path,
                                                             cauth_file_write_mode_t write_mode,
                                                             int atomic_write,
                                                             unsigned long long* out_handle);
CAUTH_API cauth_result_t cauth_depot_fetch_key(cauth_client_t* client,
                                               unsigned long long steam_id,
                                               unsigned int app_id,
                                               unsigned int depot_id,
                                               unsigned int max_count,
                                               cauth_depot_key_response_t* out_response);
CAUTH_API cauth_result_t cauth_depot_fetch_manifest_request_code(
    cauth_client_t* client,
    unsigned long long steam_id,
    unsigned int app_id,
    unsigned int depot_id,
    unsigned long long manifest_gid,
    const char* branch,
    const char* branch_password_hash,
    unsigned int max_count,
    cauth_manifest_request_code_response_t* out_response);
CAUTH_API cauth_result_t cauth_depot_load_manifest_info(const char* input_path,
                                                        const char* depot_key_hex,
                                                        cauth_manifest_info_t* out_response);
CAUTH_API cauth_result_t cauth_depot_list_manifest_files(const char* input_path,
                                                         const char* depot_key_hex,
                                                         const char* filter_text,
                                                         unsigned int limit,
                                                         cauth_manifest_file_list_t* out_response);
CAUTH_API cauth_result_t cauth_depot_download_chunk(const char* input_path,
                                                    const char* depot_key_hex,
                                                    const char* file_path,
                                                    unsigned long long file_index,
                                                    int has_file_index,
                                                    unsigned long long chunk_index,
                                                    int process_chunk,
                                                    unsigned int max_count,
                                                    const char* output_path,
                                                    cauth_file_write_mode_t write_mode,
                                                    int atomic_write);
CAUTH_API cauth_result_t cauth_depot_start_download_chunk(const char* input_path,
                                                          const char* depot_key_hex,
                                                          const char* file_path,
                                                          unsigned long long file_index,
                                                          int has_file_index,
                                                          unsigned long long chunk_index,
                                                          int process_chunk,
                                                          unsigned int max_count,
                                                          const char* output_path,
                                                          cauth_file_write_mode_t write_mode,
                                                          int atomic_write,
                                                          unsigned long long* out_handle);
CAUTH_API cauth_result_t cauth_depot_download_file(const char* input_path,
                                                   const char* depot_key_hex,
                                                   const char* file_path,
                                                   unsigned long long file_index,
                                                   int has_file_index,
                                                   unsigned int max_count,
                                                   const char* output_path,
                                                   cauth_file_write_mode_t write_mode,
                                                   int atomic_write);
CAUTH_API cauth_result_t cauth_depot_start_download_file(const char* input_path,
                                                         const char* depot_key_hex,
                                                         const char* file_path,
                                                         unsigned long long file_index,
                                                         int has_file_index,
                                                         unsigned int max_count,
                                                         const char* output_path,
                                                         cauth_file_write_mode_t write_mode,
                                                         int atomic_write,
                                                         unsigned long long* out_handle);
CAUTH_API cauth_result_t cauth_depot_download_all_files(const char* input_path,
                                                        const char* depot_key_hex,
                                                        unsigned int max_count,
                                                        const char* output_root,
                                                        cauth_file_write_mode_t write_mode,
                                                        int atomic_write);
CAUTH_API cauth_result_t cauth_depot_start_download_all_files(const char* input_path,
                                                              const char* depot_key_hex,
                                                              unsigned int max_count,
                                                              const char* output_root,
                                                              cauth_file_write_mode_t write_mode,
                                                              int atomic_write,
                                                              unsigned long long* out_handle);
CAUTH_API cauth_result_t cauth_depot_verify_local_files(const char* input_path,
                                                        const char* depot_key_hex,
                                                        const char* local_root,
                                                        const char* filter_text,
                                                        cauth_depot_local_verify_report_t* out_response);
CAUTH_API cauth_result_t cauth_depot_start_verify_local_files(
    const char* input_path,
    const char* depot_key_hex,
    const char* local_root,
    const char* filter_text,
    unsigned long long* out_handle);
CAUTH_API cauth_result_t cauth_depot_poll_task(unsigned long long handle,
                                               cauth_depot_task_snapshot_t* out_snapshot);
CAUTH_API void cauth_depot_cancel_task(unsigned long long handle);
CAUTH_API void cauth_depot_dispose_task(unsigned long long handle);
CAUTH_API const char* cauth_depot_last_error_detail(void);

#ifdef __cplusplus
}
#endif

#endif
