# PakFu Dependencies

## Required
- C++20 toolchain
- Meson + Ninja
- Qt 6:
  - Core
  - Gui
  - Network
  - Widgets
  - Multimedia (audio playback: WAV/MP3/Ogg Vorbis, via platform codecs)

## Planned / Optional
- Qt 6 Multimedia (video playback, waveform preview)
- Custom format loaders for:
  - CIN, ROQ (video)
  - OGG (if not handled by Qt Multimedia on a platform)
- Third-party decoding libraries as needed, scoped to formats that Qt does not
  cover on all platforms

## Implemented
- Image loaders:
  - PNG, JPEG: Qt 6 (QtGui)
  - TGA: built-in decoder (uncompressed + RLE; true-color, grayscale, color-mapped)
  - PCX: built-in decoder (RLE; 8bpp paletted, 24-bit, 16-color)
  - WAL: built-in decoder (Quake II; requires `pics/colormap.pcx` palette; previews all mip levels)

- Archive support:
  - PAK: built-in reader/writer (Quake/Quake II)
  - PK3/PK4/PKZ/ZIP: built-in reader/writer via vendored miniz
  - Quake Live Beta encrypted PK3: built-in decrypt/encrypt (XOR) loader/writer
