# Integration Guide

This document is the fastest way to wire CAuth into a real project.

It answers three questions:

1. Which modules should I install?
2. How do I link them into a native desktop project?
3. How do I consume them from an Android Compose app?

## Choose Modules

The module graph is:

```text
cauth_core <- cauth_steam_auth <- cauth_steam_depot
                              <- cauth_steam_cloud
```

Use the smallest set that matches your product:

- `cauth_core`
  - secure storage/session substrate only
  - reusable auth/session infrastructure for future providers
  - multi-account repository and active-account pointer
- `cauth_core + cauth_steam_auth`
  - Steam login
  - saved session restore
  - saved account list / switch / clear
  - Steam Guard continuation
  - CM probe/logon/auth flows
- `cauth_core + cauth_steam_auth + cauth_steam_depot`
  - branch/depot/manifest discovery
  - request code and depot key retrieval
  - manifest/chunk/file download
  - whole-manifest file extraction
- `cauth_core + cauth_steam_auth + cauth_steam_cloud`
  - Steam Cloud list / verify / pull / push

## Install From a Build Tree

Build first:

```powershell
cmake --preset default
cmake --build --preset default
```

If you want the full Steam feature set, make sure zlib and zstd support are available. zlib is
needed for compressed CM multi messages; zstd is needed for VZstd depot chunks.

Install the full stack:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install
```

Install only the reusable substrate:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install-core --component Core
```

Add Steam auth into an existing prefix:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamAuth
```

Add Steam depot into an existing prefix:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamDepot
```

Add Steam cloud into an existing prefix:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamCloud
```

## Native CMake Consumer

### Core only

```cmake
find_package(CAuth CONFIG REQUIRED COMPONENTS core)

target_link_libraries(my_app
    PRIVATE
        cauth::core
        cauth::core_ffi
)
```

### Steam auth

```cmake
find_package(CAuth CONFIG REQUIRED COMPONENTS core steam_auth)

target_link_libraries(my_app
    PRIVATE
        cauth::core
        cauth::steam_auth
        cauth::core_ffi
        cauth::steam_auth_ffi
)
```

### Steam depot

```cmake
find_package(CAuth CONFIG REQUIRED COMPONENTS core steam_auth steam_depot)

target_link_libraries(my_app
    PRIVATE
        cauth::core
        cauth::steam_auth
        cauth::steam_depot
        cauth::core_ffi
        cauth::steam_auth_ffi
        cauth::steam_depot_ffi
)
```

### Steam cloud

```cmake
find_package(CAuth CONFIG REQUIRED COMPONENTS core steam_auth steam_cloud)

target_link_libraries(my_app
    PRIVATE
        cauth::core
        cauth::steam_auth
        cauth::steam_cloud
        cauth::core_ffi
        cauth::steam_auth_ffi
        cauth::steam_cloud_ffi
)
```

On Windows, if you use the FFI DLL targets, make sure the installed `bin` directory is visible at
runtime.

## Public Headers

The main public surfaces are:

```text
include/cauth/core.hpp
include/cauth/steam_auth.hpp
include/cauth/steam_depot.hpp
include/cauth/steam_cloud.hpp

include/cauth/core_ffi.h
include/cauth/steam_auth_ffi.h
include/cauth/steam_depot_ffi.h
include/cauth/steam_cloud_ffi.h
```

Use the C++ headers if your host is native C++ and can consume C++ linkage directly.

Use the `*_ffi.h` headers if your host wants a stable C ABI, JNI bridge, game engine binding, or
other foreign-function integration.

## Account Selection

CAuth separates saved accounts from the active account pointer. Product code should make account
selection explicit:

1. authenticate through `steam_auth`
2. list saved accounts
3. let the user choose the active account when more than one exists
4. run depot or cloud work after the active account is correct

Desktop CLI equivalents:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth accounts
.\build\windows-msvc-debug\cauth.exe steam auth use --steam-id 7656119...
```

Android API equivalents:

```kotlin
val accounts = client.steamAuth().listSavedAccounts()
client.steamAuth().useSavedAccount(steamId)
```

See [accounts.md](accounts.md) for the repository model and cleanup commands.

## Desktop Smoke Tests

After integrating, verify each layer independently.

### Auth

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth status
.\build\windows-msvc-debug\cauth.exe steam auth whoami
.\build\windows-msvc-debug\cauth.exe steam auth accounts
.\build\windows-msvc-debug\cauth.exe steam auth cm probe --max-count 5
```

### Depot

```powershell
.\build\windows-msvc-debug\cauth.exe steam depot branches --app-id 2868840 --max-count 10
.\build\windows-msvc-debug\cauth.exe steam depot manifests --app-id 2868840 --branch public --max-count 10
.\build\windows-msvc-debug\cauth.exe steam depot preflight --app-id 2868840 --branch public --max-count 10
.\build\windows-msvc-debug\cauth.exe steam depot key --app-id 2868840 --depot-id 2868843 --max-count 10
```

If you already have a manifest and depot key:

```powershell
.\build\windows-msvc-debug\cauth.exe steam depot all-files-download --in .\manifest.bin --depot-key <hex> --out-dir .\out --max-count 20
.\build\windows-msvc-debug\cauth.exe steam depot verify-local --in .\manifest.bin --depot-key <hex> --local-root .\out
```

### Cloud

```powershell
.\build\windows-msvc-debug\cauth.exe steam cloud list --app-id 2868840 --remote-root savegames
.\build\windows-msvc-debug\cauth.exe steam cloud verify --app-id 2868840 --local-root .\build\slay2-sync --remote-root savegames
.\build\windows-msvc-debug\cauth.exe steam cloud pull --app-id 2868840 --local-root .\build\slay2-sync --remote-root savegames --dry-run
.\build\windows-msvc-debug\cauth.exe steam cloud push --app-id 2868840 --local-root .\build\slay2-sync --remote-root savegames --dry-run
```

## Android Integration

Android does not use a "plugin" model here. CAuth is consumed as normal Gradle modules or
published Android libraries.

Current modules:

- `cauth-android-core`
- `cauth-android-steam-auth`
- `cauth-android-steam-depot`
- `cauth-android-steam-cloud`
- `cauth-android-compose`

Recommended dependency sets:

- auth-only screen:
  - `cauth-android-core`
  - `cauth-android-steam-auth`
  - optional `cauth-android-compose`
- depot screen:
  - `cauth-android-core`
  - `cauth-android-steam-auth`
  - `cauth-android-steam-depot`
  - optional `cauth-android-compose`
- cloud screen:
  - `cauth-android-core`
  - `cauth-android-steam-auth`
  - `cauth-android-steam-cloud`
  - optional `cauth-android-compose`

### Minimal app setup

Attach the runtime once:

```kotlin
class MyApp : Application() {
    override fun onCreate() {
        super.onCreate()
        CAuthAndroidRuntime.attach(this)
    }
}
```

Create a shared client:

```kotlin
val client = remember { CAuthClient.create() }
```

Then choose one of two approaches:

1. Use the shipped controllers and Compose panes.
2. Use the lower-level APIs from your own UI.

### Fastest Compose path

```kotlin
@Composable
fun LoginScreen() {
    val client = remember { CAuthClient.create() }
    val controller = rememberCAuthSteamAuthController(client = client)

    CAuthLoginPane(controller = controller)
}
```

Depot and cloud have equivalent panes:

- `CAuthSteamDepotPane`
- `CAuthSteamCloudPane`

### Host-controlled path

If you want your own Compose UI:

```kotlin
@Composable
fun DepotScreen() {
    val client = remember { CAuthClient.create() }
    val controller = rememberCAuthSteamDepotController(client = client)
    val state by controller.state.collectAsState()

    // Render your own Compose UI from controller state
}
```

### API-only path

```kotlin
suspend fun inspect(client: CAuthClient) {
    val auth = client.steamAuth()
    val depot = client.steamDepot()
    val cloud = client.steamCloud()

    val session = auth.getSavedSession()
    val accounts = auth.listSavedAccounts()
    val branches = depot.fetchBranches(appId = 2868840)
    val files = cloud.listRemoteFiles(
        SteamCloudRequest(appId = 2868840, remoteRoot = "savegames"),
        count = 20,
    )
}
```

## Recommended Rollout Order

When integrating into your own product, keep the rollout narrow:

1. `core`
2. `steam_auth`
3. saved-account selection and active-account restore
4. `steam_depot` or `steam_cloud`
5. your own host-side orchestration and UI polish

That keeps login/session issues isolated before you add content or cloud operations.

## Related Docs

- [architecture.md](architecture.md)
- [accounts.md](accounts.md)
- [packaging.md](packaging.md)
- [compose-project-integration.md](compose-project-integration.md)
- [api-reference.md](api-reference.md)
- [android-compose.md](android-compose.md)
- [steam-depot.md](steam-depot.md)
- [steam-cloud.md](steam-cloud.md)
