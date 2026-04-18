# Roadmap

## Milestone 0: Project Foundation

- CMake project scaffold.
- Native core library.
- C ABI header.
- CLI diagnostic command.
- Basic executable tests.

## Milestone 1: Authentication Spike

- Login request/result model.
- Login service that saves successful sessions.
- Steam network authenticator orchestration.
- Steam GetPasswordRSAPublicKey transport.
- Steam RSA password encryption on Windows.
- Steam BeginAuthSessionViaCredentials transport.
- Steam PollAuthSessionStatus transport.
- Interactive Steam Guard continuation flow.
- Real-account login hardening.
- Protocol message serialization plan.
- Login state machine design.
- Steam Guard prompt flow.
- Secure credential backend interface.

## Milestone 2: Session Persistence

- Platform credential storage.
- Session restore.
- Logout and token cleanup.
- Redacted diagnostics.
- Steam Directory CM endpoint discovery.

## Milestone 3: CM Session

- CM websocket transport probe.
- CM message framing.
- ClientLogon message construction.
- Client logon with saved access token.
- Heartbeat message construction.
- Heartbeat smoke send.
- Heartbeat loop and disconnect handling.
- Account/license query smoke test.

## Milestone 4: Depot Prototype

- App/depot metadata lookup.
- Manifest parsing.
- Chunk download.
- Hash verification.
- Resume support.

## Milestone 5: Android Compose Integration

- Android JNI/Kotlin binding package.
- Compose-based login flow.
- Compose-based depot task UI.
- Progress and error reporting.
