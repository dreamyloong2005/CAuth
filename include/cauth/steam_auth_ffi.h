#ifndef CAUTH_STEAM_AUTH_FFI_H
#define CAUTH_STEAM_AUTH_FFI_H

#include "cauth/core_ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cauth_login_status {
    CAUTH_LOGIN_SUCCEEDED = 0,
    CAUTH_LOGIN_STEAM_GUARD_REQUIRED = 1,
    CAUTH_LOGIN_FAILED = 2,
    CAUTH_LOGIN_UNSUPPORTED = 3
} cauth_login_status_t;

typedef enum cauth_login_platform_type {
    CAUTH_LOGIN_PLATFORM_STEAM_CLIENT = 0,
    CAUTH_LOGIN_PLATFORM_WEB_BROWSER = 1,
    CAUTH_LOGIN_PLATFORM_MOBILE_APP = 2
} cauth_login_platform_type_t;

typedef struct cauth_app_id_probe {
    unsigned long long app_id;
    int valid;
    const char* default_branch;
    const char* status;
} cauth_app_id_probe_t;

typedef struct cauth_login_request {
    const char* account_name;
    const char* password;
    const char* steam_guard_code;
    const char* device_name;
    int remember_session;
    int platform_type;
} cauth_login_request_t;

typedef struct cauth_login_result {
    cauth_login_status_t status;
    cauth_result_t result;
    const char* message;
    unsigned long long steam_id;
    const char* account_name;
} cauth_login_result_t;

typedef struct cauth_capabilities {
    int web_api_auth_transport;
    int cm_websocket_transport;
    int password_rsa_encryptor;
    int depot_content_decrypt;
    int android_secure_store_bridge;
} cauth_capabilities_t;

typedef struct cauth_cm_probe_result {
    int ok;
    const char* endpoint;
    const char* status;
} cauth_cm_probe_result_t;

typedef struct cauth_cm_logon_result {
    int ok;
    const char* endpoint;
    const char* status;
    unsigned int eresult;
    unsigned int eresult_extended;
    unsigned int heartbeat_seconds;
    unsigned long long steam_id;
} cauth_cm_logon_result_t;

typedef struct cauth_saved_session {
    int present;
    unsigned long long steam_id;
    const char* account_name;
    int has_refresh_token;
    int has_access_token;
    unsigned long long created_at_unix_seconds;
} cauth_saved_session_t;

typedef struct cauth_auth_session {
    unsigned long long steam_id;
    const char* account_name;
    const char* refresh_token;
} cauth_auth_session_t;

typedef struct cauth_webapi_rsa_key {
    int present;
    const char* modulus_hex;
    const char* exponent_hex;
    unsigned long long timestamp;
} cauth_webapi_rsa_key_t;

typedef struct cauth_webapi_begin_session_request {
    const char* account_name;
    const char* encrypted_password;
    unsigned long long encryption_timestamp;
    const char* steam_guard_code;
    const char* device_name;
    int remember_login;
    int platform_type;
} cauth_webapi_begin_session_request_t;

typedef struct cauth_webapi_begin_session_response {
    int present;
    unsigned long long client_id;
    const char* request_id_base64;
    unsigned long long steam_id;
    double interval_seconds;
    int confirmation_count;
    int has_remote_confirmation;
    int guard_code_allowed;
    const char* error_message;
} cauth_webapi_begin_session_response_t;

typedef struct cauth_webapi_poll_session_request {
    unsigned long long client_id;
    const char* request_id_base64;
} cauth_webapi_poll_session_request_t;

typedef struct cauth_webapi_poll_session_response {
    int present;
    const char* refresh_token;
    const char* access_token;
    const char* account_name;
    int had_remote_interaction;
} cauth_webapi_poll_session_response_t;

typedef struct cauth_webapi_generate_access_token_request {
    unsigned long long steam_id;
    const char* refresh_token;
} cauth_webapi_generate_access_token_request_t;

typedef struct cauth_webapi_generate_access_token_response {
    int present;
    const char* access_token;
    const char* refresh_token;
} cauth_webapi_generate_access_token_response_t;

CAUTH_API cauth_result_t cauth_probe_app_id(unsigned long long app_id,
                                            cauth_app_id_probe_t* out_probe);
CAUTH_API cauth_result_t cauth_get_capabilities(cauth_capabilities_t* out_capabilities);
CAUTH_API cauth_result_t cauth_cm_probe(cauth_cm_probe_result_t* out_probe);
CAUTH_API cauth_result_t cauth_cm_logon(cauth_client_t* client,
                                        cauth_cm_logon_result_t* out_result);
CAUTH_API cauth_result_t cauth_auth_get_saved_session(cauth_client_t* client,
                                                      cauth_saved_session_t* out_session);
CAUTH_API cauth_result_t cauth_auth_clear_saved_session(cauth_client_t* client);
CAUTH_API cauth_result_t cauth_auth_save_session(cauth_client_t* client,
                                                 const cauth_auth_session_t* session);
CAUTH_API cauth_result_t cauth_auth_parse_password_rsa_response(
    const char* json,
    cauth_webapi_rsa_key_t* out_key);
CAUTH_API cauth_result_t cauth_auth_build_begin_session_form_body(
    const cauth_webapi_begin_session_request_t* request,
    const char** out_form_body);
CAUTH_API cauth_result_t cauth_auth_parse_begin_session_response(
    const void* bytes,
    unsigned long long size,
    cauth_webapi_begin_session_response_t* out_response);
CAUTH_API cauth_result_t cauth_auth_build_poll_session_form_body(
    const cauth_webapi_poll_session_request_t* request,
    const char** out_form_body);
CAUTH_API cauth_result_t cauth_auth_parse_poll_session_response(
    const void* bytes,
    unsigned long long size,
    cauth_webapi_poll_session_response_t* out_response);
CAUTH_API cauth_result_t cauth_auth_build_generate_access_token_form_body(
    const cauth_webapi_generate_access_token_request_t* request,
    const char** out_form_body);
CAUTH_API cauth_result_t cauth_auth_parse_generate_access_token_response(
    const void* bytes,
    unsigned long long size,
    cauth_webapi_generate_access_token_response_t* out_response);
CAUTH_API cauth_result_t cauth_auth_login_password(cauth_client_t* client,
                                                   const cauth_login_request_t* request,
                                                   cauth_login_result_t* out_result);

#ifdef __cplusplus
}
#endif

#endif
