# Steam Depot

`cauth_steam_depot` owns Steam app info, depot metadata, manifest access, content download, and
local verification.

## What the module covers

- branch discovery
- depot manifest discovery
- depot key retrieval
- manifest request code retrieval
- manifest download and parse
- chunk and file download
- full-manifest extraction
- local file verification against the manifest
- depot platform metadata surfaced from appinfo

## Typical workflow

### 1. Inspect the app

```powershell
.\build\windows-msvc-debug\cauth.exe steam depot branches --app-id 2868840 --max-count 20
.\build\windows-msvc-debug\cauth.exe steam depot manifests --app-id 2868840 --branch public --max-count 20
.\build\windows-msvc-debug\cauth.exe steam depot preflight --app-id 2868840 --branch public --max-count 20
```

`preflight` is the best starting point because it joins branch, depot, and manifest information into
one snapshot.

## Platform-aware depot selection

Each resolved depot can expose:

- `platform=<label>`
- `from_app=<appid>` when the depot is shared from another app
- `shared_install=true` when Steam marks it as shared-install content

The platform label is built from appinfo depot config:

- `oslist`
- `osarch`
- shared-depot metadata

Examples:

- `windows`
- `windows/x64`
- `linux`
- `android`
- `shared`
- `windows+linux`

If you are downloading content for one platform only, prefer a depot whose label matches that
platform.

## 2. Fetch auth materials

Once you have a target depot and manifest:

```powershell
.\build\windows-msvc-debug\cauth.exe steam depot key --app-id 2868840 --depot-id <depot_id> --max-count 20
.\build\windows-msvc-debug\cauth.exe steam depot manifest-code --app-id 2868840 --depot-id <depot_id> --manifest-gid <manifest_gid> --branch public --max-count 20
```

Common failure hints:

- `eresult=9`
  - usually ownership or entitlement is missing for that depot
- `eresult=8`
  - depot id, manifest gid, branch, or request pairing is invalid
- `eresult=15`
  - the saved Steam session is not valid for the CM path and should be refreshed by logging in again

## 3. Download the manifest

```powershell
.\build\windows-msvc-debug\cauth.exe steam depot manifest-download --depot-id <depot_id> --manifest-gid <manifest_gid> --request-code <request_code> --out .\build\manifest.bin --max-count 20
```

Then inspect it:

```powershell
.\build\windows-msvc-debug\cauth.exe steam depot manifest-info --in .\build\manifest.bin --depot-key <depot_key_hex>
.\build\windows-msvc-debug\cauth.exe steam depot file-list --in .\build\manifest.bin --depot-key <depot_key_hex> --limit 50
```

Directory-only entries are filtered out of download and verify workflows even if they exist in the
raw manifest tree.

## 4. Download content

### One chunk

```powershell
.\build\windows-msvc-debug\cauth.exe steam depot chunk-download --in .\build\manifest.bin --file "relative/path" --chunk-index 0 --out .\build\chunk.bin --depot-key <depot_key_hex> --process --max-count 20
```

### One file

```powershell
.\build\windows-msvc-debug\cauth.exe steam depot file-download --in .\build\manifest.bin --file "relative/path" --depot-key <depot_key_hex> --out .\build\out\relative\path --max-count 20
```

### Everything

```powershell
.\build\windows-msvc-debug\cauth.exe steam depot all-files-download --in .\build\manifest.bin --depot-key <depot_key_hex> --out-dir .\build\out --max-count 20
```

`all-files-download` recreates the manifest directory structure under `--out-dir`.

## 5. Verify local files

```powershell
.\build\windows-msvc-debug\cauth.exe steam depot verify-local --in .\build\manifest.bin --depot-key <depot_key_hex> --local-root .\build\out
```

Verification checks:

- file exists
- file size matches
- chunk structure lines up with the manifest
- binary payload matches expected chunk data

This is the command to use when you want confidence that an extracted depot tree is actually
complete.

## Android example behavior

The Android example app uses the same native depot module and adds a few workflow helpers:

- manifest workflow prefers a Windows-tagged depot when several candidates are returned
- `Prepare Key + Code` clears stale key and request-code state before refetching
- verify, chunk/file download, and all-files download all run through progress-aware task cards

For the Android-side UI and controller details, see [android-compose.md](android-compose.md).
