# Architecture

CAuth is organized as a layered native toolkit with explicit module boundaries.

## Module Graph

```text
cauth_core
  <- cauth_steam_auth
      <- cauth_steam_depot
      <- cauth_steam_cloud
```

## Modules

### `cauth_core`

Owns the cross-platform substrate that other auth providers can reuse:

- session and credential models
- multi-account repository model keyed by `provider + subject_id`
- secure storage repository interfaces and platform-backed implementations
- JWT parsing and shared auth result types
- HTTP / websocket abstractions
- platform runtime bridges
- shared application-facing orchestration utilities

`cauth_core` should not contain Steam-specific protocol concepts. It can store generic records such
as `provider`, `subject_id`, tokens, and timestamps, but it should not know which Steam session
flavor is preferred for CM or depot operations.

### `cauth_steam_auth`

Owns Steam account authentication and authenticated CM access:

- Steam password and auth transport flows
- Steam Guard continuation and polling
- Web login and cookie/token follow-up
- CM directory lookup, probe, logon, and auth-oriented service calls
- Steam session persistence through `cauth_core`
- Steam-specific session selection rules, such as preferring `steam-client` auth material for CM,
  depot, cloud auto-selection, and most native Steam workflows

This module depends on `cauth_core`.

### `cauth_steam_depot`

Owns Steam content and depot workflows:

- app info and branch discovery
- platform-tagged depot metadata derived from appinfo
- depot key retrieval
- manifest request codes and manifest download
- chunk download, decompression, decryption, hashing, and file extraction
- local file verification against downloaded content

This module depends on `cauth_steam_auth`, and therefore transitively on `cauth_core`.

### `cauth_steam_cloud`

Owns Steam cloud-save synchronization workflows:

- remote file enumeration
- cloud pull / push orchestration
- local-vs-remote verification
- conflict handling and reconciliation
- transfer-task progress and cancellation hooks

This module depends on `cauth_steam_auth`, and therefore transitively on `cauth_core`.

## Public Surfaces

```text
include/cauth/core.hpp
include/cauth/steam_auth.hpp
include/cauth/steam_depot.hpp
include/cauth/steam_cloud.hpp

include/cauth/core_ffi.h
include/cauth/steam_auth_ffi.h
include/cauth/steam_depot_ffi.h
include/cauth/steam_cloud_ffi.h
```

Each module exports its own native C++ surface and its own C ABI surface.

## Directory Map

```text
src/core/            shared runtime and storage infrastructure
src/steam/auth/      Steam auth-only logic
src/steam/cm/        shared Steam CM protocol support used by Steam modules
src/steam/depot/     depot/content workflows
src/steam/cloud/     cloud-save sync workflows
src/ffi/             split FFI implementations
src/cli/             thin CLI frontend over application modules
```

## Account Boundary

The account repository belongs to `cauth_core` because account storage is provider-neutral. The
interpretation of a stored session belongs to the owning provider module.

For Steam, `cauth_steam_auth` maps a generic stored account to Steam-specific session types:

- `steam-client`
- `web-browser`
- `mobile-app`

Depot and cloud modules should not parse repository internals directly. They should ask
`cauth_steam_auth` for usable auth material.

## Packaging Model

CMake install/export is also split by module:

- `Core`
- `SteamAuth`
- `SteamDepot`
- `SteamCloud`
- `Cli`

The generated `CAuthConfig.cmake` loads whichever module target files are present in the install
prefix and verifies the dependency chain before reporting requested components as found.
