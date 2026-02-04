# PakFu Agent Rules

These rules guide automated or human contributors working on this repository.
Keep them aligned with the goals in `README.md`.

## Core Requirements
- Use C++ for all production code.
- Use Qt6 Widgets or QML for the UI.
- Preserve cross-platform support (Windows, macOS, Linux).
- Maintain a documented CLI interface for all key actions.
- Keep Meson + Ninja as the build system.

## File Format Support
- Ensure the app can load/preview common formats:
  - Images: pcx, wal, png, tga, jpg
  - Audio: wav, ogg
  - Video: cin, roq
  - Text/config: cfg and similar plain-text formats
- Prefer modular, extensible loaders to add new formats.

## Engineering Practices
- Prefer modern C++ (C++20) and portable libraries.
- Avoid OS-specific APIs unless guarded and justified.
- Keep the UI responsive with async loading for large archives.
- Update docs and help text whenever behavior changes.
- Update `docs/DEPENDENCIES.md` when adding or changing libraries.
- Keep `VERSION` and GitHub release assets in sync with the updater rules.
- Follow `.editorconfig` for formatting (tabs for C++, spaces for config files).

## Contribution Boundaries
- Do not introduce alternate UI frameworks.
- Do not remove or deprecate the CLI without a replacement plan.
- Do not hardcode platform-only paths or assumptions.
