# Security

CAuth should never store a Steam password in plaintext.

## Credential Handling

- Prefer reusable Steam session or refresh material over saving passwords.
- Store sensitive material in platform secure storage.
- Keep UI and CLI frontends out of direct secret storage.
- Do not log passwords, tokens, sentry data, or raw authentication payloads.
- When several accounts are saved, treat the chosen `subject_id` as sensitive session-selection
  state in your product.
- Keep raw cookie dumps and Web API responses out of committed logs.

## Platform Storage Plan

- Windows: Credential Manager or DPAPI.
- Android: Android Keystore-backed app storage through a JNI/Kotlin bridge.
- macOS: Keychain.
- Linux: Secret Service through libsecret, with an explicit encrypted-file fallback only when needed.

## Development Defaults

Early development may use a mock credential backend for tests. Any file-based backend must be
clearly marked as unsafe for production unless it encrypts data with platform-protected keys.

## Git Hygiene

Before publishing or sharing a branch, check that these are not staged:

- Steam passwords or account-specific notes
- refresh tokens, access tokens, cookies, or request ids
- downloaded manifests or depot payloads from private accounts
- cloud-save test data that belongs to a real user
