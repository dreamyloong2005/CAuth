# Steam Cloud

`cauth_steam_cloud` sits beside `cauth_steam_auth` and currently supports Steam Cloud as a
CM-backed workflow. CAuth still keeps the standalone web-cookie tooling around for diagnostics, but
the Cloud `web` backend itself is intentionally treated as unsupported.

## CLI shape

```powershell
.\build\windows-msvc-debug\cauth.exe steam cloud list --steam-id 7656119... --app-id 440
.\build\windows-msvc-debug\cauth.exe steam cloud verify --steam-id 7656119... --app-id 440 --local-root .\build\tf2-cloud
.\build\windows-msvc-debug\cauth.exe steam cloud pull --steam-id 7656119... --app-id 440 --local-root .\build\tf2-cloud --dry-run
.\build\windows-msvc-debug\cauth.exe steam cloud push --steam-id 7656119... --app-id 440 --local-root .\build\tf2-cloud --dry-run
```

Common flags:

- `--remote-root <path>` narrows the cloud scope to one subtree in Steam Cloud.
- `--conflict-policy <default|local-wins|remote-wins|newer-wins|fail>` controls pull and push
  conflict handling. `fail-on-conflict` is also accepted as an alias.
- `--backend <auto|web|cm>` chooses the Cloud backend. In `auto` mode, CAuth prefers a saved
  `steam-client` session when the same Steam account also has web/mobile sessions saved.
  `--backend web` is currently expected to fail fast with an unsupported-backend message.
- `--steam-id <id>` selects which saved Steam account to use. It is required even when an
  `--access-token` override is supplied.
- `--access-token <token>` overrides the saved login session token for direct token experiments.
  It does not make the unsupported `--backend web` path usable.
- `--refresh-token <token>` can be supplied with `--steam-id <id>` for stateless cloud calls.
- `--delete-remote-orphans` allows `push` to remove remote files that do not exist locally.
- `--include-extra-local` makes `verify` report local-only files explicitly.

On the current Windows desktop path, `pull` and `push` print live byte progress in the CLI while
network transfer is happening.

Cloud verify results expose both aggregate counters and per-entry detail arrays, so hosts can see
exactly which remote/local files matched, diverged, or were missing.

## Auth material

Steam Cloud has one practical session source today:

1. normal `steam auth login`
   - gives CAuth the saved `steam-client` auth material used by `--backend auto`
   - selected per command with `--steam-id <id>`

`steam auth login-web`

- still drives the web-cookie / finalize-login path directly
- is useful for auth diagnostics such as `steam auth web-cookies`
- should not be treated as a working Steam Cloud backend right now

`steam auth web-cookies` is a good diagnostic command when you want to confirm that the saved web
session is healthy before testing auth-related web flows. It does not imply that Steam Cloud web
enumeration/download is usable.

When several Steam accounts are saved, inspect the repository and pass the desired SteamID:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth accounts
```

Use `--backend auto` unless you are debugging a specific path. `--backend cm` forces CM Cloud
service calls. `--backend web` is currently unsupported because Steam does not expose a stable
usable web enumerate/download path for Steam Cloud with the auth material available to
`login-web`.

If you want to inspect the store-page view anyway, use the separate diagnostic command:

```powershell
.\build\windows-msvc-debug\cauth.exe steam cloud web-page-list --steam-id 7656119... --app-id 2868840 --remote-root savegames
```

`steam cloud web-page-list` is read-only and best-effort. It is useful for debugging what a
web-authenticated store page can see, but it does not imply that web-backed Cloud list, pull, or
push are supported.

## Acceptance Script

Use [scripts/steam-cloud-acceptance.ps1](../scripts/steam-cloud-acceptance.ps1) to run the whole flow
in one shot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\steam-cloud-acceptance.ps1 `
    -SteamId 7656119... `
    -AppId 440 `
    -LocalRoot .\build\tf2-cloud `
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
    -SteamId 7656119... `
    -AppId 2868840 `
    -LocalRoot .\build\slay2-cloud `
    -RemoteRoot savegames `
    -PlanOnly

# Run the read-only checks against the live account session.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\steam-cloud-acceptance.ps1 `
    -SteamId 7656119... `
    -AppId 2868840 `
    -LocalRoot .\build\slay2-cloud `
    -RemoteRoot savegames `
    -ConflictPolicy newer-wins

# Run the full flow and allow remote orphan deletion.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\steam-cloud-acceptance.ps1 `
    -SteamId 7656119... `
    -AppId 2868840 `
    -LocalRoot .\build\slay2-cloud `
    -RemoteRoot savegames `
    -DeleteRemoteOrphans `
    -RunPush
```

Logs are written to `build/logs/steam-cloud-acceptance-*.log` by default. You can override that
with `-LogPath`.

