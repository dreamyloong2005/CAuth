# Contributing

Thanks for contributing to CAuth.

## Ground rules

- Keep module boundaries intact:
  - `cauth_core`
  - `cauth_steam_auth`
  - `cauth_steam_depot`
  - `cauth_steam_cloud`
- Prefer putting generic behavior into `core` first, then letting platform layers provide only the
  platform-specific bridge pieces.
- Do not copy GPL reference code into CAuth.

## Development setup

From a Visual Studio 2022 developer shell:

```powershell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

For Android:

```powershell
cd .\android
.\gradlew.bat :example-android:assembleDebug
```

## Before opening a PR

Please run the smallest relevant validation set:

- auth-only changes
  - desktop auth smoke tests
- depot changes
  - depot preflight / manifest / verify path
- cloud changes
  - cloud list / verify / pull / push dry-run path
- Android changes
  - rebuild the touched Android modules or `:example-android:assembleDebug`

Useful docs:

- [docs/testing.md](docs/testing.md)
- [docs/integration.md](docs/integration.md)
- [docs/api-reference.md](docs/api-reference.md)

## Style

- C++: follow the existing repository style and keep edits scoped
- Kotlin: prefer host-friendly, modular APIs over example-specific shortcuts
- Docs: update docs when public behavior, commands, or module boundaries change
