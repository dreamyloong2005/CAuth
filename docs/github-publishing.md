# GitHub Publishing

This document is the final check before publishing CAuth to GitHub.

## Source hygiene

The public repository should contain:

- source code
- docs
- scripts
- tests
- required vendored source dependencies

The public repository should not contain:

- build outputs
- Android Gradle caches
- local test downloads
- local manifests, request captures, or debug dumps
- external reference source snapshots used only for research
- unrelated local binaries

## Current repository rules

The root `.gitignore` excludes:

- `build/`
- Android `build/` / `.gradle/` / `.kotlin/`
- `reference/`
- local temp files
- test download artifacts
- local dependency build caches under `.deps/`
- stray local binaries and generated artifacts

## Before creating the GitHub repo

1. Decide the repository visibility
   - private first is the safer default
2. Confirm the license files are still intentional
   - CAuth is MIT licensed
   - vendored dependencies keep their own upstream licenses
3. Confirm that `reference/` stays local-only research material
4. Confirm no account data, tokens, manifests, or downloaded content remain in the working tree
5. Confirm the published version
   - current development version is `0.3.0`
   - version source of truth is `project(CAuth VERSION ...)` in `CMakeLists.txt`

## Suggested first push flow

```powershell
git init -b main
git add .
git commit -m "Initial import"
git remote add origin <your-github-repo-url>
git push -u origin main
```

## Tagging a release

Use a leading `v` tag after validation:

```powershell
git tag v0.3.0
git push origin v0.3.0
```

Before tagging, run the checks in [versioning.md](versioning.md) and [testing.md](testing.md).

## If Git breaks later

Git for Windows keeps a system config at:

```text
D:/Git/etc/gitconfig
```

If that file is corrupted again, rebuild it as a minimal valid config before debugging anything
higher-level.

## Recommended repository metadata

At minimum, keep these files at the repo root:

- `README.md`
- `CONTRIBUTING.md`
- `SECURITY.md`
- `LICENSE`

## Companion docs

- [index.md](index.md)
- [getting-started.md](getting-started.md)
- [testing.md](testing.md)
- [versioning.md](versioning.md)
