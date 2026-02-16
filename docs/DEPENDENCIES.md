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
  - OpenGLWidgets (model preview: MDL/MD2/MD3/IQM/MD5/LWO/OBJ)
  - Multimedia (audio/video playback via available backend codecs; prefers FFmpeg when available)
  - MultimediaWidgets (video output: QVideoWidget)

## Planned / Optional
- Vulkan runtime + drivers (enables the Vulkan 3D preview renderer; OpenGL remains the fallback)
- OGG (if not handled by Qt Multimedia on a platform)
- Third-party decoding libraries as needed, scoped to formats that Qt does not
  cover on all platforms

## Implemented
- Multimedia playback:
  - Qt 6 Multimedia + MultimediaWidgets (codec support depends on backend; prefers FFmpeg when available):
    - Audio: WAV, MP3, Ogg Vorbis
    - Video: formats supported by the installed backend (e.g. OGV/Theora when FFmpeg is available)
  - Built-in cinematic playback:
    - CIN, ROQ

- Image loaders:
  - PNG, JPEG: Qt 6 (QtGui)
  - TGA: built-in decoder (uncompressed + RLE; true-color, grayscale, color-mapped)
  - PCX: built-in decoder (RLE; 8bpp paletted, 24-bit, 16-color)
  - LMP: built-in decoder (Quake QPIC + conchars.lmp/colormap.lmp/pop.lmp raw lumps; palette.lmp preview; most LMPs require `gfx/palette.lmp`)
  - MIP: built-in decoder (Quake/GoldSrc MIPTEX; requires a 256-color Quake palette via `gfx/palette.lmp` or a WAD `palette` lump)
  - WAL: built-in decoder (Quake II; requires `pics/colormap.pcx` palette; previews all mip levels)
  - DDS: built-in decoder (uncompressed masks + BC1/BC2/BC3/BC4/BC5, including DX10 headers)

- Archive support:
  - PAK: built-in reader/writer (Quake/Quake II)
  - WAD: built-in reader/writer (WAD2), reader/extractor (WAD3)
  - PK3/PK4/PKZ/ZIP: built-in reader/writer via vendored miniz
  - Quake Live Beta encrypted PK3: built-in decrypt/encrypt (XOR) loader/writer

- Cinematics (built-in decoders; used for thumbnails + playback widget):
  - CIN: id Quake II cinematic (PAL8 + optional PCM audio)
  - ROQ: id RoQ (vector quantized video + optional RoQ DPCM audio)

- idTech asset inspectors (built-in metadata parsers):
  - SPR: Quake/idTech2 sprite decode + animated preview + metadata/frame table summary
  - SP2 / SPR2: Quake II sprite frame references resolve to animated preview + metadata/frame table summary
  - DM2: Quake II demo packet stream summary
  - AAS: Quake III bot navigation header + lump summary
  - QVM: Quake III VM bytecode header + segment summary

- Text/script preview:
  - Includes Quake-style script/config assets such as `cfg`, `arena`, `bot`, `skin`, `shaderlist`, `shader`, and `menu`
