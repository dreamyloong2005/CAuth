# Compose Project Integration

This guide is for integrating CAuth into an existing Android app that already uses Jetpack Compose.

It assumes:

- your app is the host product
- this repository stays checked out as `../CAuth` or another stable local path
- you want to consume the Android modules directly from source for now

## 1. Pick the module set

For a Compose host app, choose the smallest useful set:

- auth only
  - `:cauth-android-core`
  - `:cauth-android-steam-auth`
  - optional `:cauth-android-compose`
- auth + depot
  - `:cauth-android-core`
  - `:cauth-android-steam-auth`
  - `:cauth-android-steam-depot`
  - optional `:cauth-android-compose`
- auth + cloud
  - `:cauth-android-core`
  - `:cauth-android-steam-auth`
  - `:cauth-android-steam-cloud`
  - optional `:cauth-android-compose`
- full stack
  - all of the above

## 2. Wire the modules into your host `settings.gradle.kts`

The simplest source-level integration is to point your host build at the CAuth Android modules
directly:

```kotlin
include(":cauth-android-core")
project(":cauth-android-core").projectDir = file("../CAuth/android/cauth-android-core")

include(":cauth-android-steam-auth")
project(":cauth-android-steam-auth").projectDir = file("../CAuth/android/cauth-android-steam-auth")

include(":cauth-android-steam-depot")
project(":cauth-android-steam-depot").projectDir = file("../CAuth/android/cauth-android-steam-depot")

include(":cauth-android-steam-cloud")
project(":cauth-android-steam-cloud").projectDir = file("../CAuth/android/cauth-android-steam-cloud")

include(":cauth-android-compose")
project(":cauth-android-compose").projectDir = file("../CAuth/android/cauth-android-compose")
```

If you only need part of the stack, include only those modules.

## 3. Mirror the repository setup

Your host project's repository list should include at least:

```kotlin
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}
```

If your team develops in mainland China, copying the Aliyun mirrors from
`CAuth/android/settings.gradle.kts` is still a good idea.

## 4. Add dependencies in the host app module

### Auth only

```kotlin
dependencies {
    implementation(project(":cauth-android-core"))
    implementation(project(":cauth-android-steam-auth"))
}
```

### Auth plus optional ready-made panes

```kotlin
dependencies {
    implementation(project(":cauth-android-core"))
    implementation(project(":cauth-android-steam-auth"))
    implementation(project(":cauth-android-compose"))
}
```

### Full stack

```kotlin
dependencies {
    implementation(project(":cauth-android-core"))
    implementation(project(":cauth-android-steam-auth"))
    implementation(project(":cauth-android-steam-depot"))
    implementation(project(":cauth-android-steam-cloud"))
    implementation(project(":cauth-android-compose"))
}
```

## 5. Attach the runtime once

In your `Application`:

```kotlin
class MyApp : Application() {
    override fun onCreate() {
        super.onCreate()
        CAuthAndroidRuntime.attach(this)
    }
}
```

And in `AndroidManifest.xml`:

```xml
<application
    android:name=".MyApp"
    ... />
```

This is required because the native runtime needs the Android application context.

## 6. Decide how to own `CAuthClient`

`CAuthClient` is the native handle owner.

Recommended lifecycle choices:

- screen-local experiments
  - `remember { CAuthClient.create() }`
- real app
  - own one client in a `ViewModel`, presenter, or DI scope
  - reuse it across auth, depot, and cloud screens

Example:

```kotlin
@Composable
fun rememberSharedCAuthClient(): CAuthClient {
    return remember { CAuthClient.create() }
}
```

Close it when the owning scope is destroyed.

## 7. Choose one UI integration style

### A. Ready-made panes

Fastest path when you want a working debug or internal-tool surface:

```kotlin
@Composable
fun AuthScreen() {
    val client = remember { CAuthClient.create() }
    val controller = rememberCAuthSteamAuthController(client = client, closeClientOnDispose = true)
    CAuthLoginPane(controller = controller)
}
```

For depot/cloud:

- `CAuthSteamDepotPane`
- `CAuthSteamCloudPane`

### B. Host-controlled controllers

This is the recommended path for a real product UI:

```kotlin
@Composable
fun CloudScreen(client: CAuthClient) {
    val controller = rememberCAuthSteamCloudController(client)
    val state by controller.state.collectAsState()

    // Render your own Compose UI from state
}
```

### C. API-only

Skip the shipped controller and UI completely:

```kotlin
suspend fun loadData(client: CAuthClient) {
    val auth = client.steamAuth()
    val depot = client.steamDepot()
    val cloud = client.steamCloud()

    val session = auth.getSavedSession()
    val preflight = depot.fetchPreflight(appId = 2868840)
    val remote = cloud.listRemoteFiles(
        SteamCloudRequest(appId = 2868840, remoteRoot = "savegames"),
    )
}
```

## 8. Understand the Android dependency shape

There are two different dependency graphs:

### Product capability graph

```text
cauth_core <- cauth_steam_auth <- cauth_steam_depot
                              <- cauth_steam_cloud
```

### Android Gradle compile graph

```text
cauth-android-core
cauth-android-steam-auth -> cauth-android-core
cauth-android-steam-depot -> cauth-android-core
cauth-android-steam-cloud -> cauth-android-core
cauth-android-compose -> all three feature modules
```

This is intentional.

`steam_depot` and `steam_cloud` still depend on a valid saved Steam session at runtime, but they do
not need to compile against the Kotlin auth module just to use that session. The session lives in
the shared native runtime and storage layer.

## 9. Native build assumptions

Each Android feature module builds its JNI layer by pointing CMake back to the CAuth repo root.
That means:

- keep the checked-out CAuth directory structure intact
- do not copy a single Android module out of the repo and expect it to build alone
- if you relocate the repo, keep the internal relative layout unchanged

## 10. Recommended rollout into a real app

1. integrate `core` + `steam_auth`
2. get login and session restore stable
3. add `steam_depot` or `steam_cloud`
4. only then replace stock panes with your product UI

That order keeps auth/session issues from getting mixed into depot or cloud debugging.

## 11. Useful companion docs

- [android-compose.md](android-compose.md)
- [integration.md](integration.md)
- [api-reference.md](api-reference.md)
- [testing.md](testing.md)
