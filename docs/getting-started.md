# Getting Started

This guide gets a fresh CAuth workspace into a buildable, testable state as quickly as possible.

## Prerequisites

### Windows desktop

- Visual Studio 2022 Build Tools or full Visual Studio 2022, used as the MSVC sysroot
- LLVM clang 22.1.8; the workspace expects it at `D:/Dev/Tools/LLVM`
- mcpp 2026.11.17.1 or newer on `PATH`

mcpp finds the MSVC tools itself, so a Developer PowerShell is not required. If LLVM is installed
elsewhere, change `[workspace.toolchain] windows` in the root `mcpp.toml`, or set
`MCPP_TOOLCHAIN=path:<dir>` for a single build.

### Android

- Android Studio Hedgehog or newer
- Android SDK / platform tools
- mcpp available on `PATH` so Gradle can build the JNI libraries; mcpp installs the Android NDK
  it needs itself, so `ndk.dir` and `ANDROID_NDK_HOME` are not used for native builds
- A device or emulator for the Compose example app

## Compression dependencies

Two native dependencies unlock important Steam paths:

- zlib
  - required for compressed CM multi messages
  - without it, CM auth and CM-backed flows may fail with `compressed CMsgMulti requires zlib support`
- zstd
  - required for VZstd depot chunks
  - without it, some depot downloads may fail with `VZstd chunk processing requires zstd support`

The mcpp build takes zlib 1.3.2 and zstd 1.5.7 from the mcpp registry (`compat.zlib`,
`compat.zstd`) and always enables both.

## Configure and build

From the repository root:

```powershell
mcpp build
mcpp test
```

`mcpp build` uses the debug profile by default; `mcpp build --release` builds the release profile.
Outputs are rooted at:

```text
target/x86_64-windows-msvc/<fingerprint>/bin/<member>/
```

The CLI is staged with the DLLs it needs in `bin/cli/`. For the rest of this guide, assume:

```powershell
$cauth = ".\target\x86_64-windows-msvc\<fingerprint>\bin\cli\cauth.exe"
```

Replace `<fingerprint>` with the directory name mcpp created. `mcpp run cauth -- --version` also
runs the CLI without a path.

## First smoke checks

```powershell
& $cauth --version
& $cauth doctor
& $cauth steam auth status
```

`--version` should print the current native version, for example `CAuth 0.6.1`. If `doctor` and
`--version` work, the desktop toolchain is in decent shape.

## Steam auth quick test

Use a real Steam account and mobile confirmation / guard code as needed:

```powershell
$password = Read-Host "Steam password" -AsSecureString
$plain = [Runtime.InteropServices.Marshal]::PtrToStringUni(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($password)
)
$plain | & $cauth steam auth login --username your_steam_login --password-stdin --poll-attempts 48
Remove-Variable plain
Remove-Variable password
```

If you need to stop during setup, guard polling, or any other auth stage, press `Ctrl+C`. The login
commands now honor cancellation across the whole flow instead of only during the final poll loop.

Then verify the saved session:

```powershell
& $cauth steam auth status
& $cauth steam auth accounts
& $cauth steam auth whoami --steam-id 7656119...
& $cauth steam auth token-info --steam-id 7656119...
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

The Cloud pane also exposes a diagnostic-only `Web Page List` action. It helps inspect what a
web-authenticated store page can see, but it does not mean Steam Cloud web pull or push is
supported.

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
