# PakFu Dependencies

## Required
- C++20 toolchain
- Meson + Ninja
- Qt 6:
  - Core
  - Gui
  - Network
  - Widgets
  - OpenGL (Qt OpenGL helpers)
  - OpenGLWidgets (model preview: MDL/MD2/MD3/IQM/MD5)
  - Multimedia (audio playback: WAV/MP3/Ogg Vorbis, via platform codecs)

## Planned / Optional
- Qt 6 Multimedia (video playback, waveform preview)
- OGG (if not handled by Qt Multimedia on a platform)
- Third-party decoding libraries as needed, scoped to formats that Qt does not
  cover on all platforms

## Implemented
- Image loaders:
  - PNG, JPEG: Qt 6 (QtGui)
  - TGA: built-in decoder (uncompressed + RLE; true-color, grayscale, color-mapped)
  - PCX: built-in decoder (RLE; 8bpp paletted, 24-bit, 16-color)
  - LMP: built-in decoder (Quake QPIC + conchars.lmp; palette.lmp preview; most LMPs require `gfx/palette.lmp`)
  - MIP: built-in decoder (Quake/GoldSrc MIPTEX; requires a 256-color Quake palette via `gfx/palette.lmp` or a WAD `palette` lump)
  - WAL: built-in decoder (Quake II; requires `pics/colormap.pcx` palette; previews all mip levels)
  - DDS: built-in decoder (uncompressed masks + BC1/BC2/BC3/BC4/BC5, including DX10 headers)

- Archive support:
  - PAK: built-in reader/writer (Quake/Quake II)
  - WAD: built-in reader/extractor (WAD2/WAD3)
  - PK3/PK4/PKZ/ZIP: built-in reader/writer via vendored miniz
  - Quake Live Beta encrypted PK3: built-in decrypt/encrypt (XOR) loader/writer

- Cinematics (built-in decoders + preview playback):
  - CIN: id Quake II cinematic (PAL8 + optional PCM audio)
  - ROQ: id RoQ (vector quantized video + optional RoQ DPCM audio)
