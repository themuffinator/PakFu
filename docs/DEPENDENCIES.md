# PakFu Dependencies

## Required
- C++20 toolchain
- Meson + Ninja
- Qt 6:
  - Core
  - Gui
  - Widgets

## Planned / Optional
- Qt 6 Multimedia (audio/video playback, waveform preview)
- Custom format loaders for:
  - PCX, WAL, TGA (image)
  - CIN, ROQ (video)
  - OGG (if not handled by Qt Multimedia on a platform)
- Third-party decoding libraries as needed, scoped to formats that Qt does not
  cover on all platforms
