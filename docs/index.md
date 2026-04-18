# CAuth Docs

This folder is the working handbook for CAuth as a three-piece native stack:

```text
cauth_core <- cauth_steam_auth <- cauth_steam_depot
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
3. [integration.md](integration.md)
   - how to choose modules
   - native CMake consumption
   - Android/Compose integration shape
4. [architecture.md](architecture.md)
   - module boundaries
   - public surfaces
   - packaging model
5. [steam-depot.md](steam-depot.md)
   - branch/depot/manifest workflow
   - platform-aware depot selection
   - download and local verify
6. [steam-cloud.md](steam-cloud.md)
   - list / verify / pull / push flow
   - web vs CM-backed auth materials
7. [compose-project-integration.md](compose-project-integration.md)
   - how to wire CAuth into an existing Compose project
   - host `settings.gradle.kts` / dependency setup
   - `CAuthClient` lifecycle guidance
8. [api-reference.md](api-reference.md)
   - C++ / FFI / Android / CLI surface map
9. [android-compose.md](android-compose.md)
   - Android module layering
   - controllers, panes, and example app
10. [packaging.md](packaging.md)
   - install components
   - exported targets
11. [github-publishing.md](github-publishing.md)
   - pre-publish checklist
   - Git workaround notes
   - what should and should not be pushed
12. [security.md](security.md)
13. [legal-notes.md](legal-notes.md)
14. [roadmap.md](roadmap.md)

If you are integrating CAuth into a product, start with
[getting-started.md](getting-started.md), then
[testing.md](testing.md), then
[integration.md](integration.md).
