# CAuth Docs

This folder is the working handbook for CAuth as a modular native stack:

```text
cauth_core
  <- cauth_steam_auth
      <- cauth_steam_depot
      <- cauth_steam_cloud
```

Recommended reading order:

1. [getting-started.md](getting-started.md)
   - prerequisites
   - build presets
   - where binaries and APKs land
2. [testing.md](testing.md)
   - desktop smoke tests
   - Steam auth / depot / cloud verification flow
   - Android example build and logcat loop
3. [accounts.md](accounts.md)
   - saved account repository
   - active account pointer
   - CLI and Android account operations
4. [integration.md](integration.md)
   - how to choose modules
   - native CMake consumption
   - Android/Compose integration shape
5. [architecture.md](architecture.md)
   - module boundaries
   - public surfaces
   - packaging model
6. [steam-depot.md](steam-depot.md)
   - branch/depot/manifest workflow
   - platform-aware depot selection
   - download and local verify
7. [steam-cloud.md](steam-cloud.md)
   - list / verify / pull / push flow
   - web vs CM-backed auth materials
8. [compose-project-integration.md](compose-project-integration.md)
   - how to wire CAuth into an existing Compose project
   - host `settings.gradle.kts` / dependency setup
   - `CAuthClient` lifecycle guidance
9. [versioning.md](versioning.md)
   - current version
   - native version source of truth
   - Android example versioning
   - release bump checklist
10. [api-reference.md](api-reference.md)
   - C++ / FFI / Android / CLI surface map
11. [android-compose.md](android-compose.md)
   - Android module layering
   - controllers, panes, and example app
12. [packaging.md](packaging.md)
   - install components
   - exported targets
13. [github-publishing.md](github-publishing.md)
   - pre-publish checklist
   - Git workaround notes
   - what should and should not be pushed
14. [security.md](security.md)
15. [legal-notes.md](legal-notes.md)
16. [roadmap.md](roadmap.md)

If you are integrating CAuth into a product, start with
[getting-started.md](getting-started.md), then
[testing.md](testing.md), then
[accounts.md](accounts.md), then
[integration.md](integration.md).
