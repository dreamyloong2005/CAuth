# Steam Cloud

`cauth_steam_cloud` sits beside `cauth_steam_auth` and uses a valid Steam web session to read or
write Steam Cloud files for a game.

## CLI shape

```powershell
.\build\windows-msvc-debug\cauth.exe steam cloud list --app-id 440
.\build\windows-msvc-debug\cauth.exe steam cloud verify --app-id 440 --local-root .\build\tf2-sync
.\build\windows-msvc-debug\cauth.exe steam cloud pull --app-id 440 --local-root .\build\tf2-sync --dry-run
.\build\windows-msvc-debug\cauth.exe steam cloud push --app-id 440 --local-root .\build\tf2-sync --dry-run
```

Common flags:

- `--remote-root <path>` narrows the sync scope to one subtree in Steam Cloud.
- `--conflict-policy <default|local-wins|remote-wins|newer-wins|fail>` controls pull and push
  conflict handling. `fail-on-conflict` is also accepted as an alias.
- `--backend <auto|web|cm>` chooses whether cloud calls use web-backed or CM-backed auth material.
- `--access-token <token>` overrides the saved login session. If omitted, CAuth uses the current
  stored Steam auth session.
- `--refresh-token <token>` and `--steam-id <id>` allow stateless cloud calls without relying on the
  saved session store.
- `--delete-remote-orphans` allows `push` to remove remote files that do not exist locally.
- `--include-extra-local` makes `verify` report local-only files explicitly.

## Auth material

Steam Cloud supports two practical session sources:

1. normal `steam auth login`
   - usually gives CAuth enough saved auth material for `--backend auto`
2. `steam auth login-web`
   - drives the web-cookie / finalize-login path directly
   - useful when you intentionally want a web-flavored session

`steam auth web-cookies` is a good diagnostic command when you want to confirm that the saved web
session is healthy before testing cloud operations.

## Acceptance Script

Use [scripts/steam-cloud-acceptance.ps1](../scripts/steam-cloud-acceptance.ps1) to run the whole flow
in one shot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\steam-cloud-acceptance.ps1 `
    -AppId 440 `
    -LocalRoot .\build\tf2-sync `
    -RemoteRoot remote `
    -PlanOnly
```

The script always runs these stages in order:

1. `steam cloud list`
2. `steam cloud verify`
3. `steam cloud pull --dry-run`
4. `steam cloud push --dry-run`
5. optional real `steam cloud push`

The real upload step only runs if you add `-RunPush`.

Useful examples:

```powershell
# Preview the exact commands without touching Steam Cloud.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\steam-cloud-acceptance.ps1 `
    -AppId 2868840 `
    -LocalRoot .\build\slay2-sync `
    -RemoteRoot savegames `
    -PlanOnly

# Run the read-only checks against the live account session.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\steam-cloud-acceptance.ps1 `
    -AppId 2868840 `
    -LocalRoot .\build\slay2-sync `
    -RemoteRoot savegames `
    -ConflictPolicy newer-wins

# Run the full flow and allow remote orphan deletion.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\steam-cloud-acceptance.ps1 `
    -AppId 2868840 `
    -LocalRoot .\build\slay2-sync `
    -RemoteRoot savegames `
    -DeleteRemoteOrphans `
    -RunPush
```

Logs are written to `build/logs/steam-cloud-acceptance-*.log` by default. You can override that
with `-LogPath`.

