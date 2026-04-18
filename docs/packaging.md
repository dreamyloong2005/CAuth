# Packaging

## Install Components

CAuth currently exposes five CMake install components:

- `Core`
- `SteamAuth`
- `SteamDepot`
- `SteamCloud`
- `Cli`

The intended layering is:

```text
Core <- SteamAuth <- SteamDepot
                  <- SteamCloud
```

`Cli` is optional and sits on top of the native libraries.

## Optional native dependencies

For the full Steam feature set, package zlib and zstd support alongside the core modules:

- zlib enables compressed CM multi message handling
- zstd enables VZstd depot chunk processing

Without them, some auth, depot, and cloud paths can build successfully but fail at runtime when
Steam serves compressed payloads.

## Example Installs

Install the full stack:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install
```

Install only the reusable auth/session substrate:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install-core --component Core
```

Add Steam authentication support into an existing prefix:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamAuth
```

Add Steam depot support into an existing prefix:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamDepot
```

Add Steam cloud support into an existing prefix:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component SteamCloud
```

Install the CLI without changing the native library split:

```powershell
cmake --install .\build\windows-msvc-debug --config Debug --prefix .\build\install --component Cli
```

## Exported Targets

The installed package exports these targets when their owning component is present:

- `cauth::core`
- `cauth::steam_auth`
- `cauth::steam_depot`
- `cauth::steam_cloud`
- `cauth::steam`
- `cauth::core_ffi`
- `cauth::steam_auth_ffi`
- `cauth::steam_depot_ffi`
- `cauth::steam_cloud_ffi`

## Consumer Examples

Core only:

```cmake
find_package(CAuth CONFIG REQUIRED COMPONENTS core)
target_link_libraries(my_app PRIVATE cauth::core cauth::core_ffi)
```

Steam auth only after `Core` is installed:

```cmake
find_package(CAuth CONFIG REQUIRED COMPONENTS core steam_auth)
target_link_libraries(my_app PRIVATE cauth::steam_auth cauth::steam_auth_ffi)
```

Full Steam stack:

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

Steam auth plus cloud:

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

## Runtime Note

On Windows, consumers that link the FFI DLL targets must make the installed `bin` directory
available at runtime so the corresponding DLLs can be found.
