# Getting Started

This guide gets a fresh CAuth workspace into a buildable, testable state as quickly as possible.

## Prerequisites

### Windows desktop

- Visual Studio 2022 Build Tools or full Visual Studio 2022
- CMake 3.22 or newer
- A Developer PowerShell or another shell with MSVC environment variables loaded

### Android

- Android Studio Hedgehog or newer
- Android SDK / platform tools
- Android NDK installed at the version expected by `CMakePresets.json`
- A device or emulator for the Compose example app

## Compression dependencies

Two optional native dependencies unlock important Steam paths:

- zlib
  - required for compressed CM multi messages
  - without it, CM auth and CM-backed flows may fail with `compressed CMsgMulti requires zlib support`
- zstd
  - required for VZstd depot chunks
  - without it, some depot downloads may fail with `VZstd chunk processing requires zstd support`

This repository automatically uses bundled dependency trees when they are present:

```text
.deps/zlib-1.3.1
.deps/zstd-1.5.7
```

You can also point CMake at custom installs with `CAUTH_ZLIB_ROOT` and `CAUTH_ZSTD_ROOT`.

## Configure and build

From a Visual Studio 2022 developer shell:

```powershell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The default preset is a Windows debug build rooted at:

```text
build/windows-msvc-debug
```

The resulting CLI path is:

```powershell
.\build\windows-msvc-debug\cauth.exe
```

If you build with a separate Visual Studio multi-config tree, the executable is commonly under a
path like:

```powershell
.\build\windows-vs2022-debug\Debug\cauth.exe
```

## First smoke checks

```powershell
.\build\windows-msvc-debug\cauth.exe --version
.\build\windows-msvc-debug\cauth.exe doctor
.\build\windows-msvc-debug\cauth.exe steam auth status
```

`--version` should print the current native version, for example `CAuth 0.5.1`. If `doctor` and
`--version` work, the desktop toolchain is in decent shape.

## Steam auth quick test

Use a real Steam account and mobile confirmation / guard code as needed:

```powershell
$password = Read-Host "Steam password" -AsSecureString
$plain = [Runtime.InteropServices.Marshal]::PtrToStringUni(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($password)
)
$plain | .\build\windows-msvc-debug\cauth.exe steam auth login --username your_steam_login --password-stdin --poll-attempts 48
Remove-Variable plain
Remove-Variable password
```

Then verify the saved session:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth status
.\build\windows-msvc-debug\cauth.exe steam auth accounts
.\build\windows-msvc-debug\cauth.exe steam auth whoami --steam-id 7656119...
.\build\windows-msvc-debug\cauth.exe steam auth token-info --steam-id 7656119...
```

Depot, cloud, and authenticated CM commands do not use hidden global account state. Pass the
desired SteamID with `--steam-id <id>`.

## Android example app

From the repository root:

```powershell
cd .\android
.\gradlew.bat :example-android:assembleDebug
.\gradlew.bat :example-android:installDebug
```

Useful logcat filters while testing:

```powershell
adb logcat -s CAuthNative CAuthCompose
```

The example app is a real diagnostic console for auth, depot, and cloud, not just a thin demo.

Its controller state now follows a clearer task lifecycle:

- busy states like `reading`, `writing`, `downloading`, `uploading`, and `verifying`
- terminal states like `succeeded`, `failed`, and `canceled`
- automatic return to `idle`
- guaranteed `moduleTask == null` once the controller settles back to `idle`
- depot/cloud verify snapshots now include per-entry detail arrays, not just aggregate counters

## Where to go next

- [testing.md](testing.md) for a full validation checklist
- [accounts.md](accounts.md) for saved accounts and explicit subject-id behavior
- [versioning.md](versioning.md) for release and version bump rules
- [integration.md](integration.md) for native and Android consumption
- [steam-depot.md](steam-depot.md) for depot workflow details
- [steam-cloud.md](steam-cloud.md) for cloud workflow details
