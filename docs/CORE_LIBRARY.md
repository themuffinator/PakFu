# PakFu Core Library

PakFu now builds a non-UI static library target named `pakfu_core`.

This is the first stable seam for archive, format, search, game-profile, and
extension-contract code that does not depend on the desktop UI layer.

## Scope

`pakfu_core` includes:
- archive loading and extraction
- PAK/WAD/ZIP/resources backends
- archive search indexing
- image/model/cinematic/BSP parsing helpers
- game profile and auto-detect helpers
- manifest-driven extension command loading/execution

`pakfu_core` does not include:
- Qt Widgets or viewer windows
- `PakTab`, `MainWindow`, or preview widgets
- updater UI flow
- platform shell integration

## Umbrella header

The stable umbrella header is:

```text
src/pakfu_core.h
```

It aggregates the primary non-UI APIs used by the app, tests, and helper tools.

## Build target

Build just the core layer:

```sh
meson compile -C builddir pakfu_core
```

Build the core API smoke test:

```sh
meson compile -C builddir core_api_smoke_test
meson test -C builddir core-api-smoke
```

## Intended use

This target is meant to:
- reduce UI-to-backend coupling
- give tests and helper binaries a stable link target
- provide a clean place for future CLI and scripting integrations

It is not yet installed as a packaged SDK, and its API is stable by repository
contract rather than by external semantic-version promises.
