# CAuth

CAuth is a native C/C++ toolkit for Steam authentication, Steam depot downloads, and Steam Cloud
file workflows. It is designed as a set of small modules that can be installed and consumed
independently.

The current stack is:

```text
cauth_core
  <- cauth_steam_auth
      <- cauth_steam_depot
      <- cauth_steam_cloud
```

## What It Provides

- `cauth_core`
  - cross-platform client lifetime and runtime bridges
  - session repository model with multiple saved accounts addressed by `provider + subject_id`
  - secure-storage interfaces and platform-backed storage implementations
  - shared C ABI types for non-C++ hosts
- `cauth_steam_auth`
  - Steam client, web-browser, and mobile-app login entry points
  - Steam Guard polling and continuation
  - saved-account listing, explicit lookup, and clearing
  - CM directory lookup, probe, logon, and authenticated service calls
  - web-cookie / finalize-login helpers for web-backed flows
- `cauth_steam_depot`
  - branch, depot, and manifest discovery
  - platform-aware depot metadata
  - depot key and manifest request-code retrieval
  - manifest, chunk, file, and whole-depot download
  - local binary verification against depot manifests
- `cauth_steam_cloud`
  - Steam Cloud file listing
  - local-vs-remote verification
  - pull and push workflows
  - conflict policy handling
  - live transfer progress in the desktop CLI
  - progress and cancellation support on Android
  - controller-facing `moduleStatus` / `moduleTask` state with automatic return to `idle`

The command-line tool under `cauth.exe` is a diagnostic frontend over those modules. It is useful
for development and acceptance testing, but it is not required by library consumers.

## Current Version

The current development version is `0.6.1`.

Native versioning is controlled by CMake:

```cmake
project(CAuth VERSION 0.6.1)
```

CMake generates the native version header at configure time, and the CLI / C ABI read that same
value. Android's example app has its own `versionCode` / `versionName` for APK packaging.

See [docs/versioning.md](docs/versioning.md) before bumping the version.

## Repository Layout

```text
cmake/              CMake package helpers and generated-header templates
include/cauth/       Public C++ and C ABI headers
src/core/            Shared runtime, storage, session, crypto, and transport infrastructure
src/steam/auth/      Steam login, account, web auth, and CM auth workflows
src/steam/cm/        Steam CM protocol support shared by Steam modules
src/steam/depot/     Depot metadata, manifest, CDN, download, and verification workflows
src/steam/cloud/     Steam Cloud list, verify, pull, and push workflows
src/ffi/             Split C ABI implementations
src/cli/             Development CLI frontend
android/             Android JNI, Kotlin APIs, Compose UI, and example app
docs/                Integration and validation guides
tests/               Unit and smoke tests
.deps/               Vendored zlib/zstd source trees used by the build
```

`reference/` is intentionally ignored. It is only for local research snapshots and should not be
published.

## Requirements

Desktop Windows:

- Visual Studio 2022 Build Tools or full Visual Studio 2022
- CMake 3.22 or newer
- a Developer PowerShell or another shell with the MSVC environment loaded

Android:

- Android Studio or a compatible Gradle/SDK setup
- Android SDK platform tools
- Android NDK matching the Android CMake configuration
- a device or emulator for the example app

Steam compatibility dependencies:

- zlib is required for compressed CM multi messages
- zstd is required for VZstd depot chunks

The repository currently contains vendored source trees under `.deps/zlib-1.3.1` and
`.deps/zstd-1.5.7`. Build outputs and downloaded archives under `.deps/` are ignored.

## Build

From a Visual Studio 2022 developer shell:

```powershell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The default Windows build tree is:

```text
build/windows-msvc-debug
```

The CLI is normally:

```powershell
.\build\windows-msvc-debug\cauth.exe
```

## Android Example

From the repository root:

```powershell
cd .\android
.\gradlew.bat :example-android:assembleDebug
.\gradlew.bat :example-android:installDebug
```

Useful logcat filters:

```powershell
adb logcat -s CAuthNative CAuthCompose
```

The example app is a real diagnostic console for auth, depot, and cloud flows. It is also the
fastest way to validate the Android JNI and Compose layers before integrating CAuth into another
project.

## Quick CLI Flow

```powershell
$cauth = ".\build\windows-msvc-debug\cauth.exe"
& $cauth --version
& $cauth doctor
```

Sign in with the CM-backed Steam client path:

```powershell
$password = Read-Host "Steam password" -AsSecureString
$plain = [Runtime.InteropServices.Marshal]::PtrToStringUni(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($password)
)
$plain | & $cauth steam auth login --username your_steam_login --password-stdin --poll-attempts 48 --device-name "MyLauncher"
Remove-Variable plain
Remove-Variable password
```

All Steam auth login commands can now be canceled at any stage with `Ctrl+C`. Hosts using the C
ABI can request the same behavior through `cauth_auth_request_login_cancel()`, and the Android
controllers expose matching cancel hooks at the higher level.

Inspect saved accounts and choose the account explicitly for later operations:

```powershell
& $cauth steam auth status
& $cauth steam auth accounts
& $cauth steam auth whoami --steam-id 7656119...
& $cauth steam auth clear --steam-id 7656119...
```

Depot and cloud smoke checks:

```powershell
& $cauth steam auth cm probe --max-count 10
& $cauth steam depot preflight --steam-id 7656119... --app-id 2868840 --branch public --max-count 20
& $cauth steam cloud list --steam-id 7656119... --app-id 2868840 --remote-root savegames --backend auto
```

`steam depot manifest-download`, `chunk-download`, `file-download`, `all-files-download`, and
`steam cloud pull` / `push` now surface live byte progress in the desktop CLI on the WinHTTP path.

When one Steam account has multiple saved session types, depot flows and Steam Cloud `--backend auto`
prefer the `steam-client` session automatically.

Steam Cloud should currently be treated as a CM-backed feature. `steam auth login-web` remains
useful for validating the standalone web-cookie / finalize-login flow, but `steam cloud --backend web`
is intentionally reported as unsupported until Steam exposes a stable usable web enumerate/download
path again.

If you still want to inspect what the Steam store page exposes for a web-authenticated session, use
the diagnostic-only command:

```powershell
& $cauth steam cloud web-page-list --steam-id 7656119... --app-id 2868840 --remote-root savegames
```

`steam cloud web-page-list` is read-only and best-effort. It does not prove that Steam Cloud web
pull or push is usable.

On Android, the feature controllers now expose a higher-level lifecycle on top of the raw native
results:

- `moduleStatus` carries the module-defined busy or terminal state string
- `moduleTask` carries the current task summary while work is active or briefly after it completes
- terminal states such as `succeeded`, `failed`, and `canceled` automatically settle back to `idle`
- when a controller returns to `idle`, its public `moduleTask` is cleared back to `null`
- depot/cloud verify snapshots now also include per-entry detail arrays so hosts can show the exact
  files that were missing, mismatched, size-only, or extra-local

For a full validation path, use [docs/testing.md](docs/testing.md).

## Installation Components

CMake install components are split by module:

- `Core`
- `SteamAuth`
- `SteamDepot`
- `SteamCloud`
- `Cli`

Examples:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component Core
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamAuth
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamDepot
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamCloud
```

Native consumers use the exported CMake package:

```cmake
find_package(CAuth CONFIG REQUIRED COMPONENTS core steam_auth steam_depot)

target_link_libraries(my_app
    PRIVATE
        cauth::core
        cauth::steam_auth
        cauth::steam_depot
)
```

Use the `*_ffi` targets when a C ABI is needed for JNI, game engines, or other foreign-function
hosts.

## Documentation

Start here:

1. [docs/index.md](docs/index.md)
2. [docs/getting-started.md](docs/getting-started.md)
3. [docs/testing.md](docs/testing.md)
4. [docs/accounts.md](docs/accounts.md)
5. [docs/integration.md](docs/integration.md)
6. [docs/versioning.md](docs/versioning.md)
7. [docs/android-compose.md](docs/android-compose.md)
8. [docs/steam-depot.md](docs/steam-depot.md)
9. [docs/steam-cloud.md](docs/steam-cloud.md)
10. [docs/api-reference.md](docs/api-reference.md)
11. [docs/packaging.md](docs/packaging.md)

## Security And Legal Notes

CAuth must not store Steam passwords. Persisted auth material should go through the platform
session repository and secure storage layer.

The project is MIT licensed. Vendored dependencies keep their own upstream licenses. SteamKit2 and
DepotDownloader are protocol and behavior references only; GPL source must not be copied into this
project unless the whole project intentionally adopts compatible licensing.
