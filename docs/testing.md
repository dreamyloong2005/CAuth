# Testing Guide

This is the fastest end-to-end validation path for CAuth on desktop and Android.

## Build once

```powershell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

For the rest of this guide, assume:

```powershell
$cauth = ".\\build\\windows-msvc-debug\\cauth.exe"
```

## 1. Core and CLI sanity

```powershell
& $cauth --version
& $cauth doctor
```

Expected version output for the current development line:

```text
CAuth 0.6.0
```

## 2. Steam auth

### Sign in with the CM-backed path

```powershell
$password = Read-Host "Steam password" -AsSecureString
$plain = [Runtime.InteropServices.Marshal]::PtrToStringUni(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($password)
)
$plain | & $cauth steam auth login --username your_steam_login --password-stdin --poll-attempts 48 --device-name "MyLauncher"
```

Expected result:

- mobile confirmation or guard flow appears
- login eventually reports success
- `steam auth status` shows a saved session

### Follow-up auth checks

```powershell
& $cauth steam auth status
& $cauth steam auth accounts
& $cauth steam auth whoami --steam-id 7656119...
& $cauth steam auth token-info --steam-id 7656119...
& $cauth steam auth web-cookies --steam-id 7656119...
```

Use the same SteamID in depot, cloud, and authenticated CM commands.

To remove test accounts:

```powershell
& $cauth steam auth clear --steam-id 7656119...
& $cauth steam auth clear --all
```

### Web login path

Use this if you specifically want the browser-flavored session path:

```powershell
$plain | & $cauth steam auth login-web --username your_steam_login --password-stdin --poll-attempts 48
Remove-Variable plain
Remove-Variable password
```

`login-web` is useful to validate the standalone web-cookie / finalize-login flow. It is not the
recommended starting point for Steam Cloud tests; use `steam auth login` so Cloud can stay on the
CM-backed path.

## 3. CM diagnostics

```powershell
& $cauth steam auth cm servers --protocol websocket --max-count 10
& $cauth steam auth cm probe --max-count 10
& $cauth steam auth cm logon --steam-id 7656119... --max-count 10
& $cauth steam auth cm app-info --steam-id 7656119... --app-id 2868840 --max-count 10
```

`cm app-info` is also the quickest way to confirm that appinfo decoding and CM message handling are
healthy.

## 4. Depot workflow

Start from an app that you own and that exposes depots on your account. Example below uses
`2868840`.

### Discover branches and manifests

```powershell
& $cauth steam depot branches --steam-id 7656119... --app-id 2868840 --max-count 20
& $cauth steam depot manifests --steam-id 7656119... --app-id 2868840 --branch public --max-count 20
& $cauth steam depot preflight --steam-id 7656119... --app-id 2868840 --branch public --max-count 20
```

Expected result:

- branch list is returned
- preflight prints one or more depots
- each depot line may include `platform=...`, `from_app=...`, and `shared_install=...`

Prefer a depot that matches your target platform, for example `platform=windows`.

### Fetch the depot key and manifest request code

Replace `<depot_id>` and `<manifest_gid>` with values from `preflight`.

```powershell
& $cauth steam depot key --steam-id 7656119... --app-id 2868840 --depot-id <depot_id> --max-count 20
& $cauth steam depot manifest-code --steam-id 7656119... --app-id 2868840 --depot-id <depot_id> --manifest-gid <manifest_gid> --branch public --max-count 20
```

### Download and inspect the manifest

```powershell
& $cauth steam depot manifest-download --depot-id <depot_id> --manifest-gid <manifest_gid> --request-code <request_code> --out .\build\manifest.bin --max-count 20
& $cauth steam depot manifest-info --in .\build\manifest.bin --depot-key <depot_key_hex>
& $cauth steam depot file-list --in .\build\manifest.bin --depot-key <depot_key_hex> --limit 50
```

### Download content and verify locally

```powershell
& $cauth steam depot file-download --in .\build\manifest.bin --file "relative/path/from/manifest" --depot-key <depot_key_hex> --out .\build\depot-out\relative\path\from\manifest --max-count 20
& $cauth steam depot all-files-download --in .\build\manifest.bin --depot-key <depot_key_hex> --out-dir .\build\depot-out --max-count 20
& $cauth steam depot verify-local --in .\build\manifest.bin --depot-key <depot_key_hex> --local-root .\build\depot-out
```

`verify-local` compares file presence, size, chunk structure, and binary content against the
manifest, excluding directory-only entries.

The verify result now also carries per-entry details, so hosts can inspect which specific files
were `missing`, `mismatched`, or otherwise filtered instead of relying on counters alone.

Desktop CLI depot transfers now print live byte progress while the HTTP transfer is in flight on
the WinHTTP-backed path.

## 5. Cloud workflow

Cloud should currently be tested as a CM-backed flow. Use `--backend auto` first; when a
`steam-client` session is available, CAuth will prefer it automatically. `--backend web` is
currently expected to fail with an unsupported-backend message, even if `login-web` and
`steam auth web-cookies` succeed.

If you want to inspect the store-page view of Steam Cloud for diagnostics, use the separate
read-only command:

```powershell
& $cauth steam cloud web-page-list --steam-id 7656119... --app-id 2868840 --remote-root savegames
```

Treat that output as best-effort diagnostics only. It does not mean the unsupported web Cloud
backend can pull or push files.

### List and verify

```powershell
& $cauth steam cloud list --steam-id 7656119... --app-id 2868840 --remote-root savegames --backend auto
& $cauth steam cloud verify --steam-id 7656119... --app-id 2868840 --local-root .\build\slay2-cloud --remote-root savegames --backend auto
```

### Dry-run pull and push

```powershell
& $cauth steam cloud pull --steam-id 7656119... --app-id 2868840 --local-root .\build\slay2-cloud --remote-root savegames --backend auto --dry-run
& $cauth steam cloud push --steam-id 7656119... --app-id 2868840 --local-root .\build\slay2-cloud --remote-root savegames --backend auto --dry-run
```

### Real pull or push

```powershell
& $cauth steam cloud pull --steam-id 7656119... --app-id 2868840 --local-root .\build\slay2-cloud --remote-root savegames --backend auto
& $cauth steam cloud push --steam-id 7656119... --app-id 2868840 --local-root .\build\slay2-cloud --remote-root savegames --backend auto
```

Desktop CLI cloud pull and push now print live byte progress while transfer data is moving on the
WinHTTP-backed path.

Cloud verify results now also include per-entry detail arrays with remote/local path, size, hash,
and reason fields when available.

If you want an orchestrated script run, use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\steam-cloud-acceptance.ps1 `
    -SteamId 7656119... `
    -AppId 2868840 `
    -LocalRoot .\build\slay2-cloud `
    -RemoteRoot savegames `
    -PlanOnly
```

## 6. Android example app

```powershell
cd .\android
.\gradlew.bat :example-android:assembleDebug
.\gradlew.bat :example-android:installDebug
adb logcat -s CAuthNative CAuthCompose
```

Recommended Android validation sequence:

1. auth page: login, saved-session reload by SteamID, saved-account list, CM probe, CM logon
2. depot page: run the manifest workflow, fetch key + code, download manifest, inspect files,
   download one file, verify local files
3. cloud page: list, verify, dry-run pull/push, then real transfer if needed

The example app surfaces raw summaries, traces, progress cards, retry hooks, and workflow helpers,
so it is intended to be used as a real debugging console.

For the Android controller states, also sanity-check this behavior while testing:

1. start an auth / depot / cloud action and confirm `moduleStatus` enters a busy state
2. complete or cancel the action and confirm the terminal state becomes visible
3. wait a short moment and confirm the controller returns to `idle`
4. confirm `moduleTask` is cleared once the state is back at `idle`
