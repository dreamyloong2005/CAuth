# Roadmap

This file tracks the project direction after the module split. It is not a promise of dates.

## Completed Foundation

- CMake project and install components.
- Public C++ umbrella headers.
- Split C ABI headers and FFI libraries.
- Windows desktop CLI.
- Android JNI/Kotlin modules.
- Compose diagnostic example app.
- MIT license and publishing hygiene docs.

## Current Core

- Provider-neutral session model.
- Multi-account repository with explicit `provider + subject_id` selection.
- Platform-backed session repositories.
- Runtime bridges for desktop and Android.
- Shared transport and crypto utilities.

## Current Steam Auth

- Steam client login path.
- Web-browser login path.
- Mobile-app login entry point.
- Steam Guard polling and continuation.
- Saved-account list, explicit lookup, and clear flows.
- CM directory lookup, probe, logon, and auth service calls.
- Web-cookie / finalize-login diagnostic path.

## Current Steam Depot

- Branch, depot, and manifest discovery.
- Platform-aware depot selection metadata.
- Depot key and manifest request-code retrieval.
- Manifest download and parsing.
- Chunk, file, and all-files download.
- zlib and zstd-backed compatibility paths.
- Local binary verification against manifest content.

## Current Steam Cloud

- Remote cloud file listing.
- Local-vs-remote verification.
- Pull and push workflows.
- Conflict policy support.
- CM and web-backed auth material paths.
- Android progress and cancellation task model.

## Next Hardening

- Finish product-facing docs for host integration.
- Tighten Android example acceptance flows.
- Add CI once the GitHub repository is stable.
- Expand platform secure storage coverage beyond Windows and Android.
- Add more protocol regression fixtures.
- Start `cauth_microsoft_auth` as the second provider to validate that `cauth_core` stays generic.
