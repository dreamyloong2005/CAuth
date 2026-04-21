# Accounts And Sessions

CAuth stores auth state as an account repository. Any operation that needs a saved login state must
be given the account subject explicitly.

For Steam, the generic key is:

```text
provider = steam
subject_id = <SteamID>
```

This keeps multi-account behavior predictable:

- repeated login for the same Steam account updates the existing account entry
- a different Steam account creates or updates its own entry
- depot, cloud, and authenticated CM commands require `--steam-id <id>`
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

Steam-specific decisions, such as preferring the `steam-client` session for CM/depot/cloud auto
work, belong
to `cauth_steam_auth`.

## Steam Session Types

Steam auth can create different session flavors:

- `steam-client`
  - produced by `steam auth login`
  - preferred for CM, depot, and cloud `--backend auto`
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

Show repository status and inspect one saved account:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth status
.\build\windows-msvc-debug\cauth.exe steam auth whoami --steam-id 7656119...
```

Run account-specific operations by passing the same SteamID:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth refresh-access --steam-id 7656119...
.\build\windows-msvc-debug\cauth.exe steam auth web-cookies --steam-id 7656119...
.\build\windows-msvc-debug\cauth.exe steam auth cm logon --steam-id 7656119...
```

Clear only one account:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth clear --steam-id 7656119...
```

Clear every saved account:

```powershell
.\build\windows-msvc-debug\cauth.exe steam auth clear --all
```

## Android API

The Android Steam auth API exposes the same explicit-account operations:

```kotlin
val auth = client.steamAuth()

val accounts = auth.listSavedAccounts()
val session = auth.getSavedSession(steamId)
auth.clearSavedAccount(steamId)
auth.clearAllSavedAccounts()
```

The Compose controller keeps a `savedAccounts` snapshot. Selecting an account fills the SteamID
input used by later saved-session operations; it does not change global state.

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

For product code, treat account choice as caller-owned state:

1. sign in or restore accounts through `steam_auth`
2. let the user choose an account and keep its `subject_id` in your own UI or profile state
3. pass that `subject_id` / SteamID into depot, cloud, CM, and saved-session calls

This keeps the depot and cloud modules dependent on auth capability, not on hidden global account
selection.
