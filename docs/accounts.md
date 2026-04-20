# Accounts And Sessions

CAuth stores auth state as an account repository plus one active account pointer.

This keeps multi-account behavior explicit:

- repeated login for the same Steam account updates the existing account entry
- a different Steam account creates or updates its own entry
- depot, cloud, and CM commands use the active Steam account unless a command accepts explicit
  stateless auth material
- clearing one account does not remove other saved accounts

## Core Model

`cauth_core` owns the generic repository model. It does not know what a Steam ticket, Steam Guard
flow, CM session, or depot key is.

The core repository stores session records with generic fields:

- `provider`
- `subject_id`
- `account_name`
- `refresh_token`
- `access_token`
- token presence flags
- creation timestamp

The active pointer is also generic:

```text
provider + subject_id
```

For Steam, `provider` is `steam` and `subject_id` is the SteamID.

Steam-specific decisions, such as preferring the `steam-client` session for CM/depot work, belong
to `cauth_steam_auth`.

## Steam Session Types

Steam auth can create different session flavors:

- `steam-client`
  - produced by `steam auth login`
  - preferred for CM, depot, and most native Steam flows
- `web-browser`
  - produced by `steam auth login-web`
  - useful for web-cookie and finalize-login validation
- `mobile-app`
  - exposed for completeness and future work

The repository can hold multiple session records internally, but the public account list is
deduplicated at the account level. Logging in again with the same account and same session type
updates that slot instead of creating a duplicate user-facing account.

## CLI Commands

List saved accounts:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth accounts
```

Show the active account:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth status
.\build\windows-msvc-debug\cauth.exe steam auth whoami
```

Switch the active account:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth use --steam-id 7656119...
```

Clear only one account:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth clear --steam-id 7656119...
```

Clear every saved account:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth clear --all
```

Clear the currently active Steam account:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth clear
```

## Android API

The Android Steam auth API exposes the same account operations:

```kotlin
val auth = client.steamAuth()

val accounts = auth.listSavedAccounts()
auth.useSavedAccount(steamId)
auth.clearSavedAccount(steamId)
auth.clearAllSavedAccounts()
```

The Compose controller keeps a `savedAccounts` snapshot and exposes:

- `loadSavedAccounts()`
- `useSavedAccount(steamId)`
- `clearSavedSession()`

Use the controller for diagnostics or internal tools. For a production UI, call the API directly
from your own state layer.

## Device Name Rule

When a caller passes `--device-name`, CAuth sends that name unchanged.

If no device name is provided, CAuth uses:

```text
CAuth
```

This rule is intentionally simple. Product integrations can choose their own caller-facing device
name without CAuth adding a suffix.

## Practical Integration Rule

For product code, treat the active Steam account as the default auth context:

1. sign in or restore accounts through `steam_auth`
2. let the user choose the active account if more than one is saved
3. run depot/cloud operations after the active account is correct

This keeps the depot and cloud modules dependent on auth capability, not on a UI-specific account
selector.
