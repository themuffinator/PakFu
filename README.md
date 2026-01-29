# PakFu

PakFu is a modern, cross-platform PAK file manager written in C++ with a Qt6
Widgets or QML user interface and a full command-line interface (CLI). It is
designed for fast browsing, previewing, and managing classic game asset bundles
while remaining intuitive for new users.

## Project Goals
- Deliver a modern, intuitive UI using Qt6 Widgets or QML.
- Provide a first-class CLI for scripting and automation.
- Run consistently on Windows, macOS, and Linux.
- Read and preview common PAK-adjacent asset types:
  - Images: pcx, wal, png, tga, jpg
  - Audio: wav, ogg
  - Video: cin, roq
  - Text/config: cfg and similar plain-text formats
- Keep performance and stability high for large archives.
- Maintain clean, well-documented C++ code with minimal platform-specific
  branching.

## Non-Goals (for now)
- In-app editing of complex binary formats beyond basic metadata.
- Bundling game-specific proprietary tooling.
- Relying on platform-exclusive UI features.

## Product Rules
- The UI must be Qt6 Widgets or QML (no alternate UI frameworks).
- The app must be cross-platform and avoid OS-locked dependencies.
- The CLI must remain a supported, documented entry point.
- File format support should be additive and modular (plugins or modules).
- All new features should include documentation updates.

## Tech Stack Expectations
- C++ (modern standard, prefer C++20).
- Qt6 (Widgets or QML).
- Meson + Ninja for builds.

## Auto-Update
PakFu checks GitHub Releases for updates. Configure the repository with
`-Dgithub_repo=owner/name` when running Meson so the updater knows where to look.
Release assets should include a platform-appropriate installer:
- Windows: `.exe` or `.msi`
- macOS: `.dmg` or `.pkg`
- Linux: `.AppImage` (preferred)

See `docs/RELEASES.md` for versioning and release automation details.

## Dependencies
See `docs/DEPENDENCIES.md` for the current baseline and planned format loaders.

## Build (Meson + Ninja)
```sh
meson setup build --backend ninja -Dgithub_repo=owner/name -Dupdate_channel=stable
meson compile -C build
```

## Run (GUI or CLI)
```sh
./build/pakfu
./build/pakfu --cli --help
```

## Repository Notes
This repository is intentionally minimal right now. As implementation starts,
keep the goals and rules above aligned with design and build choices.
