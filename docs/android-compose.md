# Android Compose Integration

This document describes the current Android integration surface for CAuth and the recommended way
to wire it into a Compose app.

## Modules

The Android tree under `android/` is split into six Gradle modules:

- `cauth-android-core`
  - Android runtime attach/detach
  - HTTP / WebSocket / crypto bridges
  - `CAuthClient`
- `cauth-android-steam-auth`
  - Steam auth/session models
  - `CAuthSteamAuthApi`
  - `CAuthSteamAuthController`
- `cauth-android-steam-depot`
  - Steam depot models
  - `CAuthSteamDepotApi`
  - `CAuthSteamDepotController`
- `cauth-android-steam-cloud`
  - Steam cloud models
  - `CAuthSteamCloudApi`
  - `CAuthSteamCloudController`
- `cauth-android-compose`
  - `rememberCAuthSteamAuthController(...)`
  - `rememberCAuthSteamDepotController(...)`
  - `rememberCAuthSteamCloudController(...)`
  - ready-made Compose panes for auth, depot, and cloud
  - smaller reusable section composables for depot and cloud
- `example-android`
  - sample host app

The dependency direction is:

```text
cauth-android-core <- cauth-android-steam-auth <- cauth-android-steam-depot
cauth-android-core <- cauth-android-steam-auth <- cauth-android-steam-cloud
cauth-android-compose sits above the feature modules as an optional UI layer
example-android consumes the feature modules plus cauth-android-compose
```

## Recommended Layering

For a real app, the preferred split is:

1. Create or own a `CAuthClient` in the host app.
2. Add only the Steam feature modules you need.
3. Create feature controllers for the screen lifetime only where you want stock behavior.
4. Observe each controller's `state`.
5. Either:
   - render the ready-made panes from `cauth-android-compose`, or
   - build your own Compose screen from the smaller sections, or
   - ignore the shipped UI and bind directly to the feature APIs from your own UI.

That keeps UI replaceable while auth/session/runtime behavior stays in the lower modules.

## Minimal Setup

In your Android app module:

```kotlin
dependencies {
    implementation(project(":cauth-android-core"))
    implementation(project(":cauth-android-steam-auth"))
    implementation(project(":cauth-android-compose"))
}
```

In your `Application` or startup path, attach the Android runtime once:

```kotlin
class MyApp : Application() {
    override fun onCreate() {
        super.onCreate()
        CAuthAndroidRuntime.attach(this)
    }
}
```

## Fastest Compose Usage

If you want the built-in auth pane:

```kotlin
@Composable
fun LoginScreen() {
    val client = remember { CAuthClient.create() }
    val controller = rememberCAuthSteamAuthController(client = client)

    CAuthLoginPane(controller = controller)
}
```

`rememberCAuthSteamAuthController(...)` will close the client on dispose by default.

For depot/cloud, the compose module also exports:

- `CAuthSteamDepotPane`
- `CAuthSteamCloudPane`
- `rememberCAuthSteamDepotController(client)`
- `rememberCAuthSteamCloudController(client)`

## Recommended Host-Controlled Usage

If the host app wants to own the controller explicitly:

```kotlin
@Composable
fun LoginScreen() {
    val client = remember { CAuthClient.create() }
    val scope = rememberCoroutineScope()
    val controller = remember(client, scope) { CAuthSteamAuthController(client, scope) }
    val state by controller.state.collectAsState()

    Column {
        CAuthSteamAuthHeader(nativeVersion = state.nativeVersion)
        CAuthSteamAuthForm(state = state, controller = controller)
        CAuthSteamAuthPlatformSelector(
            selectedPlatform = state.loginPlatform,
            onPlatformSelected = controller::setLoginPlatform,
        )
        CAuthSteamAuthActionButtons(controller = controller)
        CAuthSteamAuthStatus(
            statusText = state.statusText,
            moduleStatus = state.moduleStatus,
            moduleTask = state.moduleTask,
        )
        CAuthSteamAuthTrace(traceLines = state.traceLines)
        CAuthSteamAuthResults(state = state)
    }
}
```

This is still the intended long-term integration shape for auth UI.

## Host-Level Steam APIs

If you do not want the shipped controller/UI, bind directly to the feature APIs:

```kotlin
import com.cauth.android.CAuthClient
import com.cauth.android.steam.auth.steamAuth
import com.cauth.android.steam.cloud.SteamCloudRequest
import com.cauth.android.steam.cloud.steamCloud
import com.cauth.android.steam.depot.steamDepot

suspend fun example(client: CAuthClient) {
    val auth = client.steamAuth()
    val depot = client.steamDepot()
    val cloud = client.steamCloud()

    val steamId = 76561198000000000L
    val saved = auth.getSavedSession(steamId)
    val branches = depot.fetchBranches(steamId = steamId, appId = 440)
    val files = cloud.listRemoteFiles(
        SteamCloudRequest(appId = 2868840, steamId = steamId, remoteRoot = "savegames"),
        count = 20,
    )
}
```

## Controller Contracts

`CAuthSteamAuthController` currently owns:

- editable form fields
- selected login platform
- status text
- `moduleStatus`
- `moduleTask`
- in-flight busy flag
- login result
- saved session snapshot
- saved account list snapshot
- CM probe result
- CM logon result
- rolling trace lines
- native version string

Actions currently exposed:

- `login()`
- `loadSavedSession()`
- `loadSavedAccounts()`
- `selectSavedAccount(steamId)`
- `clearSavedSession()`
- `probeCm()`
- `logonCm()`
- field setters for account, password, guard code, device name, and platform

`CAuthSteamDepotController` currently owns:

- `appId`, `steamId`, `branch`, `maxCount`
- `depotId`, `manifestGid`, `requestCode`, `branchPasswordHash`
- manifest output path / manifest path / depot key
- file filter and file list limit
- selected manifest file / chunk index / chunk output path / file output path / all-files output root
- branch/manifests/preflight snapshots
- depot key snapshot
- manifest request code snapshot
- manifest info snapshot
- manifest file list snapshot
- local verify snapshot
- `moduleStatus`
- `moduleTask`
- download task snapshot with progress/cancel state
- status text
- busy flag
- rolling trace lines

Actions currently exposed:

- `fetchBranches()`
- `fetchManifests()`
- `fetchPreflight()`
- `fetchDepotKey()`
- `fetchManifestRequestCode()`
- `prepareKeyAndCodeSelection()`
- `useManifestSelection()`
- `useManifestFile()`
- `downloadManifest()`
- `loadManifestInfo()`
- `listManifestFiles()`
- `verifyLocalFiles()`
- `downloadChunk()`
- `downloadFile()`
- `downloadAllFiles()`
- `cancelActiveDownload()`

`CAuthSteamCloudController` currently owns:

- `appId`, `steamId`, `localRoot`, `remoteRoot`, `accessToken`
- `count`, `startIndex`
- `dryRun`, `deleteRemoteOrphans`, `verifyIncludeExtraLocal`, `conflictPolicy`
- remote file list snapshot
- latest local verify snapshot
- latest pull/push result
- `moduleStatus`
- `moduleTask`
- transfer task snapshot with progress/cancel state
- status text
- busy flag
- rolling trace lines

Controller state semantics:

- busy states such as `reading`, `writing`, `queued`, `downloading`, `uploading`, `verifying`, and
  `canceling` stay visible while work is active
- terminal states such as `succeeded`, `failed`, and `canceled` remain visible briefly so the host
  can render a stable outcome
- controllers then return to `idle` automatically
- once a controller is back at `idle`, its public `moduleTask` is guaranteed to be `null`

Actions currently exposed:

- `listRemoteFiles()`
- `verifyLocalFiles()`
- `pull()`
- `push()`
- `cancelActiveTransfer()`

## Ready-Made Compose Sections

The compose module exports these reusable pieces:

- `CAuthSteamAuthHeader`
- `CAuthSteamAuthForm`
- `CAuthSteamAuthPlatformSelector`
- `CAuthSteamAuthActionButtons`
- `CAuthSteamAuthStatus`
- `CAuthSteamAuthTrace`
- `CAuthSteamAuthResults`
- `CAuthLoginPane`
- `CAuthSteamDepotPane`
- `CAuthSteamCloudPane`
- `CAuthSteamDepotQuerySection`
- `CAuthSteamDepotDownloadSetupSection`
- `CAuthSteamDepotManifestInspectSection`
- `CAuthSteamDepotContentSection`
- `CAuthSteamDepotResultsSection`
- `CAuthSteamCloudRequestSection`
- `CAuthSteamCloudOptionSection`
- `CAuthSteamCloudActionSection`
- `CAuthSteamCloudResultsSection`

Use `CAuthLoginPane` when you want the stock surface.
Use the smaller sections when you want host-defined layout with shared behavior.

## Current Scope

The shipped Compose auth surface covers Steam auth/session and CM diagnostics:

- password login
- Steam Guard continuation
- saved-session inspection/clear
- saved-account listing and SteamID selection
- CM probe
- CM logon

The shipped Compose depot/cloud panes currently focus on fast manual verification:

- depot branches
- depot manifests
- depot preflight
- platform-tagged depot selection
- depot key / manifest request code
- manifest download / inspect / file listing / local verify
- chunk / file / all-files download with progress and cancel
- cloud list / verify
- cloud pull with progress and cancel
- cloud push with progress and cancel

The sample host app also seeds practical defaults for Android testing:

- depot/cloud app id preset buttons
- default private app-files roots for manifest/chunk/file/all-files outputs
- copy-summary and copy-trace helpers for moving results into bug reports or chat
- status cards and collapsible raw-result blocks for auth/depot/cloud
- one-click manifest flow and cloud list->pull/push flow buttons for common success paths

## Build

From the repository root:

```powershell
cd .\android
.\gradlew.bat :example-android:assembleDebug
```

Install the sample app:

```powershell
.\gradlew.bat :example-android:installDebug
```

## Debugging

Useful logcat filters:

```powershell
adb logcat -s CAuthNative CAuthCompose
```

`CAuthNative` comes from the JNI layer.
`CAuthCompose` is emitted by the controller-driven auth flow and UI helpers.
