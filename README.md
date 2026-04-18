# CAuth

CAuth is a native C/C++ toolkit for secure authentication storage, Steam sign-in, and Steam depot
download workflows.

The project is intentionally split into four installable modules:

- `cauth_core`: cross-platform auth/session infrastructure and platform bridges.
- `cauth_steam_auth`: Steam login, Steam Guard continuation, CM access, and Web/API auth flows.
- `cauth_steam_depot`: Steam app/depot metadata, manifests, CDN download, verification, and file extraction.
- `cauth_steam_cloud`: Steam cloud-save list/pull/push workflows.

That dependency chain is strict:

```text
cauth_core <- cauth_steam_auth <- cauth_steam_depot
                              <- cauth_steam_cloud
```

## Current Layout

```text
include/cauth/       Public C++ and C ABI entry headers
src/core/            Shared runtime, storage, crypto, transport abstractions
src/steam/auth/      Steam authentication logic
src/steam/cm/        Steam CM protocol support used by auth/depot
src/steam/depot/     Steam depot and content download logic
src/steam/cloud/     Steam cloud-save synchronization logic
src/ffi/             Split FFI adapters for core / steam_auth / steam_depot / steam_cloud
src/cli/             Development and diagnostic CLI
tests/               Unit and install-consumer smoke tests
```

## Build

Open a Visual Studio 2022 developer shell, then run:

```powershell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

For full Steam coverage, make sure zlib and zstd support are available. This repository will pick up
bundled trees under `.deps/zlib-1.3.1` and `.deps/zstd-1.5.7` automatically when they exist. zlib is
needed for compressed CM multi messages, and zstd is needed for VZstd depot chunks.

## Start Here

If you are preparing to consume CAuth in a real project, use these docs in order:

1. [docs/index.md](docs/index.md)
2. [docs/getting-started.md](docs/getting-started.md)
3. [docs/testing.md](docs/testing.md)
4. [docs/integration.md](docs/integration.md)
5. [docs/architecture.md](docs/architecture.md)
6. [docs/android-compose.md](docs/android-compose.md)
7. [docs/compose-project-integration.md](docs/compose-project-integration.md)
8. [docs/api-reference.md](docs/api-reference.md)
9. [docs/steam-depot.md](docs/steam-depot.md)
10. [docs/steam-cloud.md](docs/steam-cloud.md)
11. [docs/packaging.md](docs/packaging.md)
12. [docs/github-publishing.md](docs/github-publishing.md)

## CLI

Common commands:

```powershell
.\build\windows-msvc-debug\cauth.exe --version
.\build\windows-msvc-debug\cauth.exe doctor
.\build\windows-msvc-debug\cauth.exe steam auth status
.\build\windows-msvc-debug\cauth.exe steam auth whoami
.\build\windows-msvc-debug\cauth.exe steam auth cm servers --protocol websocket --max-count 5
.\build\windows-msvc-debug\cauth.exe steam auth cm probe --max-count 5
.\build\windows-msvc-debug\cauth.exe steam auth cm logon --max-count 5
.\build\windows-msvc-debug\cauth.exe steam depot branches --app-id 440 --max-count 5
.\build\windows-msvc-debug\cauth.exe steam depot verify-local --in .\manifest.bin --local-root .\out
.\build\windows-msvc-debug\cauth.exe steam cloud list --app-id 440
.\build\windows-msvc-debug\cauth.exe steam cloud verify --app-id 440 --local-root .\build\tf2-sync
.\build\windows-msvc-debug\cauth.exe steam cloud pull --app-id 440 --local-root .\build\tf2-sync --dry-run
.\build\windows-msvc-debug\cauth.exe steam cloud push --app-id 440 --local-root .\build\tf2-sync --dry-run
```

Test Steam login with a real account:

```powershell
$password = Read-Host "Steam password" -AsSecureString
$plain = [Runtime.InteropServices.Marshal]::PtrToStringUni(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($password)
)
$plain | .\build\windows-msvc-debug\cauth.exe steam auth login --username your_steam_login --password-stdin
Remove-Variable plain
Remove-Variable password
```

Web-browser login remains available as a separate path:

```powershell
$plain | .\build\windows-msvc-debug\cauth.exe steam auth login-web --username your_steam_login --password-stdin
```

Mobile-app login is also exposed:

```powershell
$plain | .\build\windows-msvc-debug\cauth.exe steam auth login-mobile --username your_steam_login --password-stdin
```

If Steam Guard is slow to reach the phone, increase polling:

```powershell
$plain | .\build\windows-msvc-debug\cauth.exe steam auth login --username your_steam_login --password-stdin --poll-attempts 48
```

The device name shown by Steam is normalized with a `_CAuth` suffix. For example,
`--device-name "MyLauncher"` becomes `MyLauncher_CAuth`.

## Module Installation

Install everything:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install
```

Install only `core`:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install-core --component Core
```

Install only `steam_auth` on top of an existing prefix that already contains `core`:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamAuth
```

Install only `steam_depot` on top of an existing prefix that already contains `core` and `steam_auth`:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamDepot
```

Install only `steam_cloud` on top of an existing prefix that already contains `core` and `steam_auth`:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamCloud
```

The exported CMake package loads installed module targets dynamically and preserves the dependency
chain. A consumer can request only what it needs:

```cmake
find_package(CAuth CONFIG REQUIRED COMPONENTS core)
target_link_libraries(my_app PRIVATE cauth::core cauth::core_ffi)
```

Or the full Steam stack:

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

Or auth plus sync without depot:

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

If you link an FFI DLL target, remember to make the installed `bin` directory visible at runtime on
Windows.

## Android Compose

An Android multi-module scaffold now lives under `android/`:

- `cauth-android-core`: JNI + Kotlin SDK over the native CAuth modules
- `cauth-android-compose`: reusable Compose UI surface
- `example-android`: test app for login/session/CM verification

Open `android/` in Android Studio or run Gradle from that directory:

```powershell
cd .\android
.\gradlew.bat help
.\gradlew.bat :example-android:assembleDebug
```

The Android Gradle settings include Aliyun mirrors ahead of `google()` / `mavenCentral()` to make
dependency resolution more reliable in mainland China.

The recommended Compose integration shape is now:

- `CAuthClient` in `cauth-android-core` for raw native access
- `CAuthSteamAuthController` in `cauth-android-core` for auth/session state and actions
- `cauth-android-compose` for optional ready-made Compose UI

For the full Android/Compose integration guide, see [docs/android-compose.md](docs/android-compose.md).

For the integration-focused entry point that covers desktop and Android together, see
[docs/integration.md](docs/integration.md).

For the first-time setup path, build presets, and dependency notes, see
[docs/getting-started.md](docs/getting-started.md).

For an end-to-end validation checklist, see [docs/testing.md](docs/testing.md).

For the Steam depot flow, platform-tagged depot selection, and local verification, see
[docs/steam-depot.md](docs/steam-depot.md).

For the Steam Cloud sync flow and the acceptance script, see [docs/steam-cloud.md](docs/steam-cloud.md).

## Validation

The repository includes install-consumer smoke tests for:

- full package consumption from an installed prefix
- `core`-only consumption from a `--component Core` install

## Reference Projects

- SteamKit2: protocol and behavior reference
- DepotDownloader: depot workflow reference

Their licenses are treated as research constraints. CAuth should not copy GPL source into the
project.
