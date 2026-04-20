#ifndef CAUTH_FFI_BASE_H
#define CAUTH_FFI_BASE_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(CAUTH_BUILD_SHARED)
#define CAUTH_API __declspec(dllexport)
#else
#define CAUTH_API
#endif
#elif defined(CAUTH_BUILD_SHARED)
#define CAUTH_API __attribute__((visibility("default")))
#else
#define CAUTH_API
#endif

typedef enum cauth_result {
    CAUTH_OK = 0,
    CAUTH_ERROR_INVALID_ARGUMENT = 1,
    CAUTH_ERROR_OUT_OF_MEMORY = 2,
    CAUTH_ERROR_INTERNAL = 100
} cauth_result_t;

typedef struct cauth_client cauth_client_t;

typedef struct cauth_version {
    int major;
    int minor;
    int patch;
    const char* text;
} cauth_version_t;

typedef struct cauth_session_record {
    int present;
    const char* provider;
    const char* subject_id;
    const char* account_name;
    const char* refresh_token;
    const char* access_token;
    int has_refresh_token;
    int has_access_token;
    unsigned long long created_at_unix_seconds;
} cauth_session_record_t;

typedef struct cauth_session_list {
    const cauth_session_record_t* sessions;
    unsigned long long count;
} cauth_session_list_t;

#ifdef __cplusplus
}
#endif

#endif
