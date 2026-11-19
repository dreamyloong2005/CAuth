# Legal Notes

CAuth is intended as an original native implementation.

## Reference Boundaries

- SteamKit2 can be used to understand Steam protocol behavior and compatibility expectations.
- DepotDownloader can be used to understand depot download workflows.
- GPL source code must not be copied into CAuth unless the project intentionally adopts compatible
  licensing.

## Vendored Dependencies

CAuth vendors the LZMA SDK decoder under `third_party/lzma-sdk/`. zlib and zstd come from the mcpp
package registry (`compat.zlib`, `compat.zstd`). These dependencies retain their upstream licenses
and notices. Do not rewrite their license headers or
mix their terms into CAuth's MIT license.

## Project Hygiene

- Keep research notes separate from implementation files.
- Record major protocol observations in documentation.
- Prefer independent tests and wire-level observations over direct code translation.
