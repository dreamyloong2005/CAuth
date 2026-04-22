# Versioning

The current development version is:

```text
0.5.0
```

## Native Source Of Truth

The native C/C++ version is controlled by the CMake project declaration:

```cmake
project(CAuth VERSION 0.5.0)
```

During configure, CMake expands:

```text
cmake/cauth_version_generated.hpp.in
```

into a generated build-tree header:

```text
build/<preset>/generated/core/version_generated.hpp
```

`src/core/version.cpp` reads that generated header. This means these surfaces all report the same
native version:

- `cauth --version`
- `cauth_get_version()`
- `CAuthClient.version()` on Android
- C++ callers using `cauth::core::version()`

Do not edit generated headers in `build/`.

## Android Example Version

The Android example app has separate APK packaging metadata:

```kotlin
versionCode = 8
versionName = "0.5.0"
```

That value lives in:

```text
android/example-android/build.gradle.kts
```

The example app version should normally follow the native project version, while `versionCode`
must keep increasing for Android installs.

The Android library modules do not currently publish Maven artifacts, so they do not yet have
separate library publication versions.

## Bump Checklist

When bumping CAuth:

1. Update `project(CAuth VERSION ...)` in `CMakeLists.txt`.
2. Update `versionName` and increment `versionCode` in `android/example-android/build.gradle.kts`.
3. Update version expectations in tests that assert exact text.
4. Update this document if the current version changes.
5. Reconfigure and build the native tree.
6. Run the version and packaging checks.
7. Build the Android example app.

Recommended commands:

```powershell
cmake --preset default
cmake --build --preset default
ctest --test-dir .\build\windows-msvc-debug --output-on-failure -R "cauth_core_tests|cauth_cli_tests"
.\build\windows-msvc-debug\cauth.exe --version

cd .\android
.\gradlew.bat :example-android:assembleDebug
```

## Tagging Convention

Use Git tags with a leading `v`:

```text
v0.5.0
```

Do not tag until the repository is clean of local manifests, downloaded depot files, cloud-save
test data, and account-specific logs.
