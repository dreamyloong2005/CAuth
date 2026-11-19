# Integration Guide

This document is the fastest way to wire CAuth into a real project.

It answers three questions:

1. Which modules should I build?
2. How do I consume the native outputs?
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
  - multi-account repository keyed by `provider + subject_id`
- `cauth_core + cauth_steam_auth`
  - Steam login
  - saved session restore
  - saved account list / explicit lookup / clear
  - Steam Guard continuation
  - CM probe/logon/auth flows
- `cauth_core + cauth_steam_auth + cauth_steam_depot`
  - branch/depot/manifest discovery
  - request code and depot key retrieval
  - manifest/chunk/file download
  - whole-manifest file extraction
- `cauth_core + cauth_steam_auth + cauth_steam_cloud`
  - Steam Cloud list / verify / pull / push

## Build From the Native Tree

Build first, from the repository root:

```powershell
mcpp build
```

This builds every workspace member, which is the full stack. zlib and zstd come from the mcpp
registry and are always enabled. zlib is needed for compressed CM multi messages; zstd is needed
for VZstd depot chunks.

`mcpp build -p <package>` builds one member and its dependencies.

Build only the reusable substrate:

```powershell
mcpp build -p cauth.core-ffi
```

Build Steam auth:

```powershell
mcpp build -p cauth.steam-auth-ffi
```

Build Steam depot:

```powershell
mcpp build -p cauth.steam-depot-ffi
```

Build Steam cloud:

```powershell
mcpp build -p cauth.steam-cloud-ffi
```

Each FFI package depends on its C++ library, so building it also builds that library.

Outputs go under `target/x86_64-windows-msvc/<fingerprint>/bin/`. That directory holds every
library's DLL and import library (`.lib`). Each `bin/<member>/` directory holds that member's DLL
with the MSVC runtime DLLs, and `bin/cli/` holds `cauth.exe` with all the DLLs it needs. mcpp has
no install step yet; copy the files you need from those directories.

## Native Consumer

The current repository is consumed either by building CAuth directly with mcpp or by copying the
shared libraries from the mcpp `bin` directories and using the public headers from `include/`.
All listed libraries are shared libraries (DLL/.so).

### Core only

```text
headers: include/cauth/core.hpp, include/cauth/core_ffi.h
libraries: cauth_core, cauth_core_ffi
```

### Steam auth

```text
headers: include/cauth/core.hpp, include/cauth/steam_auth.hpp, include/cauth/steam_auth_ffi.h
libraries: cauth_core, cauth_steam_auth, cauth_core_ffi, cauth_steam_auth_ffi
```

### Steam depot

```text
headers: include/cauth/steam_depot.hpp, include/cauth/steam_depot_ffi.h
libraries: cauth_core, cauth_steam_auth, cauth_steam_depot, cauth_core_ffi, cauth_steam_depot_ffi
```

### Steam cloud

```text
headers: include/cauth/steam_cloud.hpp, include/cauth/steam_cloud_ffi.h
libraries: cauth_core, cauth_steam_auth, cauth_steam_cloud, cauth_core_ffi, cauth_steam_cloud_ffi
```

On Windows, make sure the CAuth DLLs, `zlib.dll`, and `zstd.dll` are visible at runtime, for
example by copying them next to your executable.

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

CAuth has no global account selector. Product code owns account selection and passes the selected
subject into operations:

1. authenticate through `steam_auth`
2. list saved accounts
3. let the user choose an account when more than one exists
4. run depot or cloud work with that account's SteamID

Desktop CLI equivalents, with `$cauth` set to the built `cauth.exe` as in
[getting-started.md](getting-started.md):

```powershell
& $cauth steam auth accounts
& $cauth steam auth whoami --steam-id 7656119...
```

Android API equivalents:

```kotlin
val accounts = client.steamAuth().listSavedAccounts()
val session = client.steamAuth().getSavedSession(steamId)
```

See [accounts.md](accounts.md) for the repository model and cleanup commands.

## Desktop Smoke Tests

After integrating, verify each layer independently.

### Auth

```powershell
& $cauth steam auth status
& $cauth steam auth accounts
& $cauth steam auth whoami --steam-id 7656119...
& $cauth steam auth cm probe --max-count 5
```

### Depot

```powershell
& $cauth steam depot branches --steam-id 7656119... --app-id 2868840 --max-count 10
& $cauth steam depot manifests --steam-id 7656119... --app-id 2868840 --branch public --max-count 10
& $cauth steam depot preflight --steam-id 7656119... --app-id 2868840 --branch public --max-count 10
& $cauth steam depot key --steam-id 7656119... --app-id 2868840 --depot-id 2868843 --max-count 10
```

If you already have a manifest and depot key:

```powershell
& $cauth steam depot all-files-download --in .\manifest.bin --depot-key <hex> --out-dir .\out --max-count 20
& $cauth steam depot verify-local --in .\manifest.bin --depot-key <hex> --local-root .\out
```

### Cloud

```powershell
& $cauth steam cloud list --steam-id 7656119... --app-id 2868840 --remote-root savegames
& $cauth steam cloud verify --steam-id 7656119... --app-id 2868840 --local-root .\build\slay2-cloud --remote-root savegames
& $cauth steam cloud pull --steam-id 7656119... --app-id 2868840 --local-root .\build\slay2-cloud --remote-root savegames --dry-run
& $cauth steam cloud push --steam-id 7656119... --app-id 2868840 --local-root .\build\slay2-cloud --remote-root savegames --dry-run
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

    val steamId = 76561198000000000L
    val session = auth.getSavedSession(steamId)
    val accounts = auth.listSavedAccounts()
    val branches = depot.fetchBranches(steamId = steamId, appId = 2868840)
    val files = cloud.listRemoteFiles(
        SteamCloudRequest(appId = 2868840, steamId = steamId, remoteRoot = "savegames"),
        count = 20,
    )
}
```

## Recommended Rollout Order

When integrating into your own product, keep the rollout narrow:

1. `core`
2. `steam_auth`
3. saved-account selection and explicit subject-id persistence in your product
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
