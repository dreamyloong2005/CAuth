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

Native versioning is controlled by the root `mcpp.toml` workspace manifest:

```toml
[workspace.package]
version = "0.6.1"
```

The checked-in native version header mirrors
that version, and the CLI / C ABI read that same value. Android's example app has its own `versionCode` / `versionName` for APK packaging.

See [docs/versioning.md](docs/versioning.md) before bumping the version.

## Repository Layout

```text
mcpp.toml            Native mcpp workspace manifest
mcpp/                Member manifests for the native libraries, FFI libraries, and CLI
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
```

`reference/` is intentionally ignored. It is only for local research snapshots and should not be
published.

## Requirements

Desktop Windows:

- Visual Studio 2022 Build Tools or full Visual Studio 2022, used as the MSVC sysroot
- LLVM clang 22.1.8; the workspace expects it at `D:/Dev/Tools/LLVM`
- mcpp 2026.11.17.1 or newer on `PATH`

mcpp finds the MSVC tools itself, so a Developer PowerShell is not required. If LLVM is installed
elsewhere, change `[workspace.toolchain] windows` in `mcpp.toml`, or set
`MCPP_TOOLCHAIN=path:<dir>` for a single build.

Android:

- Android Studio or a compatible Gradle/SDK setup
- Android SDK platform tools
- mcpp available on `PATH` for native JNI builds; mcpp installs the Android NDK it needs itself
- a device or emulator for the example app

Steam compatibility dependencies:

- zlib is required for compressed CM multi messages
- zstd is required for VZstd depot chunks

The build takes zlib 1.3.2 and zstd 1.5.7 from the mcpp registry (`compat.zlib`, `compat.zstd`)
as shared libraries.

## Build

From the repository root:

```powershell
mcpp build
mcpp test
```

`mcpp build --release` builds the release profile. Build outputs go under:

```text
target/x86_64-windows-msvc/<fingerprint>/bin/<member>/
```

The CLI is normally:

```powershell
mcpp run cauth -- --version
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
$cauth = ".\target\x86_64-windows-msvc\<fingerprint>\bin\cli\cauth.exe"
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

## Native Targets

mcpp workspace members are split by module. They build these artifacts:

- `cauth_core`
- `cauth_steam_auth`
- `cauth_steam_depot`
- `cauth_steam_cloud`
- `cauth`
- `cauth_core_ffi`
- `cauth_steam_auth_ffi`
- `cauth_steam_depot_ffi`
- `cauth_steam_cloud_ffi`

Use the `*_ffi` targets when a C ABI is needed for JNI, game engines, or other foreign-function
hosts. The C++ libraries and the `*_ffi` libraries are all shared libraries (DLL/.so). Android
library modules call mcpp before packaging and copy the required JNI libraries into
their generated `jniLibs` directories.

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
