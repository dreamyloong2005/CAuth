# Versioning

The current development version is:

```text
0.6.1
```

## Native Source Of Truth

The native C/C++ version is controlled by the workspace version in the root `mcpp.toml`:

```toml
[workspace.package]
version = "0.6.1"
```

The native code reads:

```text
src/core/version_generated.hpp
```

`src/core/version.cpp` reads that header. This means these surfaces all report the same
native version:

- `cauth --version`
- `cauth_get_version()`
- `CAuthClient.version()` on Android
- C++ callers using `cauth::core::version()`

Keep both `mcpp.toml` versions and `src/core/version_generated.hpp` in sync by hand when bumping
the native version.

## Android Example Version

The Android example app has separate APK packaging metadata:

```kotlin
versionCode = 11
versionName = "0.6.1"
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

1. Update `version` in `mcpp.toml`.
2. Update `versionName` and increment `versionCode` in `android/example-android/build.gradle.kts`.
3. Update `src/core/version_generated.hpp`.
4. Update this document if the current version changes.
5. Update version expectations in tests that assert exact text.
6. Build the native tree.
7. Run the version and packaging checks.
8. Build the Android example app.

Recommended commands:

```powershell
mcpp build
mcpp test core_version_tests
mcpp test cli_tests
mcpp run cauth -- --version

cd .\android
.\gradlew.bat :example-android:assembleDebug
```

## Tagging Convention

Use Git tags with a leading `v`:

```text
v0.6.1
```

Do not tag until the repository is clean of local manifests, downloaded depot files, cloud-save
test data, and account-specific logs.
