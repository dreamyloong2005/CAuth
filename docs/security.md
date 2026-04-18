# Security

CAuth should never store a Steam password in plaintext.

## Credential Handling

- Prefer reusable Steam session or refresh material over saving passwords.
- Store sensitive material in platform secure storage.
- Keep UI and CLI frontends out of direct secret storage.
- Do not log passwords, tokens, sentry data, or raw authentication payloads.

## Platform Storage Plan

- Windows: Credential Manager or DPAPI.
- Android: Android Keystore-backed app storage through a JNI/Kotlin bridge.
- macOS: Keychain.
- Linux: Secret Service through libsecret, with an explicit encrypted-file fallback only when needed.

## Development Defaults

Early development may use a mock credential backend for tests. Any file-based backend must be
clearly marked as unsafe for production unless it encrypts data with platform-protected keys.
