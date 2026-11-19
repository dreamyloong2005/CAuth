# Packaging

## mcpp Workspace

The root `mcpp.toml` is a virtual workspace. Its members (`mcpp/<member>/mcpp.toml` and
`tests/mcpp.toml`) hold only manifests; sources stay in `src/`, `include/`, and `third_party/`.
CAuth exposes native artifacts by module:

- `cauth_core`
- `cauth_steam_auth`
- `cauth_steam_depot`
- `cauth_steam_cloud`
- `cauth`
- `cauth_core_ffi`
- `cauth_steam_auth_ffi`
- `cauth_steam_depot_ffi`
- `cauth_steam_cloud_ffi`

The intended layering is:

```text
cauth_core
  <- cauth_steam_auth
      <- cauth_steam_depot
      <- cauth_steam_cloud
```

`cauth` is optional and sits on top of the native libraries. The `*_ffi` libraries expose the C
ABI used by JNI, game engines, and other foreign-function hosts.

All eight libraries are shared libraries (DLL/.so). The workspace sets
`dependency_linkage = "shared"` because mcpp does not put one static library into several DLLs.
Each `*_ffi` library links the shared C++ libraries it wraps.

Package names follow the member directories: `cauth.core`, `cauth.steam-auth`,
`cauth.steam-depot`, `cauth.steam-cloud`, `cauth.core-ffi`, `cauth.steam-auth-ffi`,
`cauth.steam-depot-ffi`, `cauth.steam-cloud-ffi`, `cauth.cli`, and `cauth.tests`.

## Native Dependencies

For the full Steam feature set, package zlib and zstd support alongside the core modules:

- zlib enables compressed CM multi message handling
- zstd enables VZstd depot chunk processing

The build takes zlib 1.3.2 and zstd 1.5.7 from the mcpp registry (`compat.zlib`, `compat.zstd`)
and always enables both. They are shared libraries too (`zlib.dll` and `zstd.dll` on Windows).

## Example Builds

Build the full desktop stack:

```powershell
mcpp build
```

Build the release profile:

```powershell
mcpp build --release
```

Build one package and its dependencies:

```powershell
mcpp build -p cauth.steam-depot-ffi
```

Outputs go under `target/x86_64-windows-msvc/<fingerprint>/bin/`, with per-member directories
under `bin/<member>/`. mcpp has no install step yet; consumers copy the DLLs and import libraries
they need from the `bin` directories.

## Android Packaging

Android library modules do not use the Android Gradle plugin native build bridge. Their Gradle
scripts apply `android/gradle/mcpp-native.gradle`, which runs
`mcpp build --target <triple> -p <package>` from the repository root for each ABI before packaging, then
copies the module's `.so` files into generated `jniLibs` folders.

The four JNI bridge packages (`cauth.android-core-jni`, `cauth.android-steam-auth-jni`,
`cauth.android-steam-depot-jni`, `cauth.android-steam-cloud-jni`) are members of the root workspace,
with manifests at `android/<module>/src/main/cpp/mcpp.toml`. On Android targets each builds its
JNI shared library. On other targets each builds a static library holding only
`android/jni_desktop_stub.cpp`, so a desktop `mcpp build` skips the JNI code.
mcpp installs the Android NDK it needs itself (`android-ndk@30.0.16248370`, minimum API level 26),
so `ndk.dir` in `local.properties` and `ANDROID_NDK_HOME` are not used for native builds.

Only `arm64-v8a` and `x86_64` are supported. mcpp has no `armeabi-v7a` or `x86` Android targets.
mcpp links `libc++_shared.so` but does not stage it, so `mcpp-native.gradle` copies it from the NDK
under `~/.mcpp/registry/data/xpkgs/xim-x-android-ndk/` for `cauth-android-core`.

The first Android build downloads the NDK (about 700 MB).

The module-to-library mapping is:

- `cauth-android-core`: `libc++_shared.so`, `libcauth_core.so`, `libcauth_core_ffi.so`, and
  `libcauth_android_core_jni.so`
- `cauth-android-steam-auth`: the zlib shared library, `libcauth_steam_auth.so`,
  `libcauth_steam_auth_ffi.so`, and `libcauth_android_steam_auth_jni.so`
- `cauth-android-steam-depot`: the zstd shared library, `libcauth_steam_depot.so`,
  `libcauth_steam_depot_ffi.so`, and `libcauth_android_steam_depot_jni.so`
- `cauth-android-steam-cloud`: `libcauth_steam_cloud.so`, `libcauth_steam_cloud_ffi.so`, and
  `libcauth_android_steam_cloud_jni.so`

The core Android module owns `libc++_shared.so`; feature modules depend on it through Gradle.

## Runtime Note

On Windows, consumers that load FFI DLLs must make the CAuth DLLs they depend on, plus `zlib.dll`
and `zstd.dll`, visible at runtime, for example by copying them from the mcpp `bin` directory next
to the executable.

See [versioning.md](versioning.md) for the version source of truth and bump checklist.
