# CAuth Memory

This file keeps a few durable project notes close to the repository.

## Module shape

```text
cauth_core
  <- cauth_steam_auth
      <- cauth_steam_depot
      <- cauth_steam_cloud
```

- `cauth_core` owns shared runtime, transport, session storage, versioning, and transfer helpers.
- `cauth_steam_auth` owns Steam login, account persistence, web-cookie helpers, and CM auth flows.
- `cauth_steam_depot` depends on auth and owns depot discovery, manifest handling, download, and verification.
- `cauth_steam_cloud` depends on auth and owns Steam Cloud list, verify, pull, and push.

## Stable behavioral notes

- Saved accounts are addressed by `provider + subject_id`; callers should pass `--steam-id` or the
  equivalent API field explicitly instead of relying on hidden global account state.
- Steam Cloud should currently be treated as a CM-backed feature. The formal web backend is
  intentionally unsupported.
- `steam cloud web-page-list` is a diagnostic-only read path. It is useful for store-page
  inspection, not as proof that web pull or push is usable.
- Steam auth login cancellation now works across the whole login flow, not just the final polling
  stage. CLI uses `Ctrl+C`; native hosts use `cauth_auth_request_login_cancel()`.
- Depot and Cloud downloads default to atomic writes and support pause-vs-cancel semantics through
  the transfer/checkpoint layer.

## Android note

The Compose example app is the main diagnostic console for Android. When behavior is unclear on the
device side, it is usually the fastest place to verify JNI, controller state, task progress, and
route selection before integrating CAuth into another host app.
