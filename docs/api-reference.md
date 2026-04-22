# API Reference Map

This document is a surface-level map of the public APIs exposed by CAuth.

It is not a full generated reference. The goal is to answer:

- which module owns which operation
- which header or Android API to import
- which CLI command matches the same workflow

## Module map

```text
cauth_core
  <- cauth_steam_auth
      <- cauth_steam_depot
      <- cauth_steam_cloud
```

## C++ umbrella headers

These are the main C++ entry points:

- `include/cauth/core.hpp`
- `include/cauth/steam_auth.hpp`
- `include/cauth/steam_depot.hpp`
- `include/cauth/steam_cloud.hpp`

They re-export the public headers for each native module.

## C FFI headers

These are the stable C ABI entry points:

- `include/cauth/core_ffi.h`
- `include/cauth/steam_auth_ffi.h`
- `include/cauth/steam_depot_ffi.h`
- `include/cauth/steam_cloud_ffi.h`

Use these for JNI, game-engine bindings, Rust/C#/Swift bridges, or other non-C++ hosts.

## Android modules

Kotlin/Android surfaces are split like this:

- `cauth-android-core`
  - `com.cauth.android.CAuthAndroidRuntime`
  - `com.cauth.android.CAuthClient`
- `cauth-android-steam-auth`
  - `com.cauth.android.steam.auth.CAuthSteamAuthApi`
  - `com.cauth.android.steam.auth.CAuthSteamAuthController`
  - `fun CAuthClient.steamAuth()`
- `cauth-android-steam-depot`
  - `com.cauth.android.steam.depot.CAuthSteamDepotApi`
  - `com.cauth.android.steam.depot.CAuthSteamDepotController`
  - `fun CAuthClient.steamDepot()`
- `cauth-android-steam-cloud`
  - `com.cauth.android.steam.cloud.CAuthSteamCloudApi`
  - `com.cauth.android.steam.cloud.CAuthSteamCloudController`
  - `fun CAuthClient.steamCloud()`
- `cauth-android-compose`
  - `CAuthLoginPane`
  - `CAuthSteamDepotPane`
  - `CAuthSteamCloudPane`
  - `rememberCAuthSteamAuthController(...)`
  - `rememberCAuthSteamDepotController(...)`
  - `rememberCAuthSteamCloudController(...)`

## Core surface

### C FFI

Defined in `include/cauth/core_ffi.h`:

- `cauth_get_version()`
- `cauth_client_create()`
- `cauth_client_destroy()`
- `cauth_result_message()`
- `cauth_session_get_saved()`
- `cauth_session_list_saved()`
- `cauth_session_clear_account()`
- `cauth_session_clear_all()`
- `cauth_session_save()`

### Android

- `CAuthAndroidRuntime.attach(context)`
- `CAuthAndroidRuntime.detach()`
- `CAuthClient.create()`
- `CAuthClient.version()`
- `CAuthClient.close()`

Core owns the provider-neutral session repository. Provider modules decide how to interpret stored
records.

`cauth_get_version()` reports the generated native project version. See
[versioning.md](versioning.md) for the source of truth.

## Steam auth surface

### C FFI

Defined in `include/cauth/steam_auth_ffi.h`.

Main request/result types:

- `cauth_login_request_t`
- `cauth_login_result_t`
- `cauth_saved_session_t`
- `cauth_cm_probe_result_t`
- `cauth_cm_logon_result_t`

Main operations:

- `cauth_probe_app_id()`
- `cauth_get_capabilities()`
- `cauth_cm_probe()`
- `cauth_cm_logon()`
- `cauth_auth_get_saved_session()`
- `cauth_auth_clear_saved_session()`
- `cauth_auth_save_session()`
- `cauth_auth_login_password()`

Web-flow helpers:

- `cauth_auth_parse_password_rsa_response()`
- `cauth_auth_build_begin_session_form_body()`
- `cauth_auth_parse_begin_session_response()`
- `cauth_auth_build_poll_session_form_body()`
- `cauth_auth_parse_poll_session_response()`
- `cauth_auth_build_generate_access_token_form_body()`
- `cauth_auth_parse_generate_access_token_response()`

### Android API

`CAuthSteamAuthApi`:

- `loginPassword(...)`
- `getSavedSession(steamId)`
- `listSavedAccounts()`
- `clearSavedSession(steamId)`
- `clearSavedAccount(steamId)`
- `clearAllSavedAccounts()`
- `probeCm()`
- `logonCm(steamId)`

`CAuthSteamAuthController`:

- owns editable login form state
- owns `moduleStatus` and `moduleTask`
- owns saved-session snapshot
- owns saved-account list snapshot
- owns CM probe/logon snapshots
- exposes `login()`, `loadSavedSession()`, `loadSavedAccounts()`, `selectSavedAccount(steamId)`,
  `clearSavedSession()`, `probeCm()`, `logonCm()`

### CLI equivalents

- `cauth steam auth login`
- `cauth steam auth login-web`
- `cauth steam auth login-mobile`
- `cauth steam auth status`
- `cauth steam auth whoami --steam-id <id>`
- `cauth steam auth accounts`
- `cauth steam auth refresh-access --steam-id <id>`
- `cauth steam auth web-cookies --steam-id <id>`
- `cauth steam auth token-info --steam-id <id>`
- `cauth steam auth clear (--steam-id <id>|--all)`
- `cauth steam auth cm ...`

## Steam depot surface

### C FFI

Defined in `include/cauth/steam_depot_ffi.h`.

Main response types:

- `cauth_app_branch_list_t`
- `cauth_depot_manifest_list_t`
- `cauth_depot_preflight_report_t`
- `cauth_depot_key_response_t`
- `cauth_manifest_request_code_response_t`
- `cauth_manifest_info_t`
- `cauth_manifest_file_list_t`
- `cauth_depot_local_verify_report_t`

Main operations:

- `cauth_depot_fetch_branches()`
- `cauth_depot_fetch_manifests()`
- `cauth_depot_fetch_preflight()`
- `cauth_depot_fetch_key()`
- `cauth_depot_fetch_manifest_request_code()`
- `cauth_depot_download_manifest()`
- `cauth_depot_load_manifest_info()`
- `cauth_depot_list_manifest_files()`
- `cauth_depot_download_chunk()`
- `cauth_depot_download_file()`
- `cauth_depot_download_all_files()`
- `cauth_depot_verify_local_files()`
- `cauth_depot_last_error_detail()`

The manifest/preflight responses also expose platform metadata:

- `platform_label`
- `os_list`
- `os_arch`
- `depot_from_app`
- `shared_install`

### Android API

`CAuthSteamDepotApi`:

- `fetchBranches(...)`
- `fetchManifests(...)`
- `fetchPreflight(...)`
- `fetchDepotKey(...)`
- `fetchManifestRequestCode(...)`
- `downloadManifest(...)`
- `loadManifestInfo(...)`
- `listManifestFiles(...)`
- `verifyLocalFiles(...)`
- `downloadChunk(...)`
- `downloadFile(...)`
- `downloadAllFiles(...)`
- `startManifestDownload(...)`
- `startChunkDownload(...)`
- `startFileDownload(...)`
- `startAllFilesDownload(...)`
- `pollDownloadTask(...)`
- `cancelDownloadTask(...)`
- `disposeDownloadTask(...)`

`CAuthSteamDepotController` adds UI-oriented orchestration such as:

- `prepareKeyAndCodeSelection(...)`
- `useManifestSelection(...)`
- `useManifestFile(...)`
- `moduleStatus` / `moduleTask`
- progress state
- cancel support
- trace lines

### CLI equivalents

- `cauth steam depot branches`
- `cauth steam depot manifests`
- `cauth steam depot preflight`
- `cauth steam depot key`
- `cauth steam depot manifest-code`
- `cauth steam depot manifest-download`
- `cauth steam depot manifest-info`
- `cauth steam depot file-list`
- `cauth steam depot verify-local`
- `cauth steam depot chunk-download`
- `cauth steam depot file-download`
- `cauth steam depot all-files-download`

## Steam cloud surface

### C FFI

Defined in `include/cauth/steam_cloud_ffi.h`.

Main request/result types:

- `cauth_steam_cloud_request_t`
- `cauth_steam_cloud_file_list_t`
- `cauth_steam_cloud_result_t`
- `cauth_steam_cloud_verify_report_t`

Main operations:

- `cauth_steam_cloud_list_remote_files(...)`
- `cauth_steam_cloud_pull(...)`
- `cauth_steam_cloud_push(...)`
- `cauth_steam_cloud_verify_local_files(...)`

### Android API

`CAuthSteamCloudApi`:

- `listRemoteFiles(...)`
- `pull(...)`
- `push(...)`
- `verifyLocalFiles(...)`
- `startPull(...)`
- `startPush(...)`
- `pollTransferTask(...)`
- `cancelTransferTask(...)`
- `disposeTransferTask(...)`

`CAuthSteamCloudController` adds:

- editable cloud request state
- `moduleStatus` / `moduleTask`
- verify state
- transfer-task progress state
- cancel support
- trace lines

For the Android controllers, `moduleStatus` is a module-defined string rather than a fixed core enum.
Busy states and terminal states are both exposed. After a short terminal-state dwell, controllers
return to `idle`, and `moduleTask` is cleared back to `null`.

### CLI equivalents

- `cauth steam cloud list`
- `cauth steam cloud verify`
- `cauth steam cloud pull`
- `cauth steam cloud push`

## Common task lookup

### Sign in and save a session

- C FFI: `cauth_auth_login_password()`
- Android API: `client.steamAuth().loginPassword(...)`
- Android controller: `CAuthSteamAuthController.login()`
- CLI: `cauth steam auth login`

### Check whether a Steam session already exists

- C FFI: `cauth_auth_get_saved_session()`
- Android API: `client.steamAuth().getSavedSession(steamId)`
- CLI: `cauth steam auth whoami --steam-id <id>`

### List or select saved Steam accounts

- C FFI:
  - `cauth_session_list_saved()`
  - `cauth_session_get_saved()`
- Android API:
  - `client.steamAuth().listSavedAccounts()`
  - `client.steamAuth().getSavedSession(steamId)`
- CLI:
  - `cauth steam auth accounts`
  - `cauth steam auth whoami --steam-id <id>`

### Inspect app branches and depots

- C FFI: `cauth_depot_fetch_branches()`, `cauth_depot_fetch_preflight()`
- Android API: `client.steamDepot().fetchBranches(...)`, `fetchPreflight(...)`
- CLI: `cauth steam depot branches`, `cauth steam depot preflight`

### Download and verify depot files

- C FFI:
  - `cauth_depot_download_all_files()`
  - `cauth_depot_verify_local_files()`
- Android API:
  - `startAllFilesDownload(...)`
  - `verifyLocalFiles(...)`
- CLI:
  - `cauth steam depot all-files-download`
  - `cauth steam depot verify-local`

### Pull or push cloud saves

- C FFI:
  - `cauth_steam_cloud_pull()`
  - `cauth_steam_cloud_push()`
- Android API:
  - `startPull(...)`
  - `startPush(...)`
- CLI:
  - `cauth steam cloud pull`
  - `cauth steam cloud push`

## Companion docs

- [compose-project-integration.md](compose-project-integration.md)
- [android-compose.md](android-compose.md)
- [accounts.md](accounts.md)
- [testing.md](testing.md)
- [steam-depot.md](steam-depot.md)
- [steam-cloud.md](steam-cloud.md)
