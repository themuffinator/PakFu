# PakFu Dependencies

## Required
- C++20 toolchain
- Meson + Ninja
- Windows builds: `DbgHelp` (system library; used for crash minidumps + stack symbolization)
- Qt 6:
  - Core
  - Gui
  - Network
  - Widgets
  - OpenGL (Qt OpenGL helpers)
  - OpenGLWidgets (model preview: MDL/MD2/MD3/MDC/MD4/MDR/SKB/SKD/MDM/GLM/IQM/MD5/LWO/OBJ)
  - Multimedia (audio/video playback via available backend codecs; prefers FFmpeg when available)
  - MultimediaWidgets (video output: QVideoWidget)

## Release Packaging Toolchain
- Windows:
  - `windeployqt` (Qt deployment)
  - WiX Toolset v3 (`heat`, `candle`, `light`) for MSI generation
- macOS:
  - `macdeployqt` (Qt deployment)
  - `pkgbuild` (native `.pkg` installer generation)
- Linux:
  - `linuxdeployqt` (downloaded in CI for AppImage packaging)
  - AppImage runtime tooling (`APPIMAGE_EXTRACT_AND_RUN` flow in CI)

## Planned / Optional
- Vulkan runtime + drivers (enables the Vulkan 3D preview renderer; OpenGL remains the fallback)
- OGG (if not handled by Qt Multimedia on a platform)
- Third-party decoding libraries as needed, scoped to formats that Qt does not
  cover on all platforms

## Implemented
- Multimedia playback:
  - Qt 6 Multimedia + MultimediaWidgets (codec support depends on backend; prefers FFmpeg when available):
    - Audio: WAV, MP3, Ogg Vorbis, BIK (backend-dependent)
    - Video: formats supported by the installed backend (e.g. OGV/Theora/Bink when available on the installed backend)
  - Built-in cinematic playback:
    - CIN, ROQ

- Image loaders:
  - PNG, JPEG: Qt 6 (QtGui)
  - TGA: built-in decoder (uncompressed + RLE; true-color, grayscale, color-mapped)
  - PCX: built-in decoder (RLE; 8bpp paletted, 24-bit, 16-color)
  - LMP: built-in decoder (Quake QPIC + Half-Life/WAD3 QPIC with embedded palettes + conchars.lmp/colormap.lmp/pop.lmp raw lumps; palette.lmp preview; external `gfx/palette.lmp` used when no embedded palette is present)
  - MIP: built-in decoder (Quake/GoldSrc MIPTEX plus raw conchars-style indexed payloads; requires a 256-color Quake palette via `gfx/palette.lmp` or a WAD `palette` lump)
  - WAL: built-in decoder (Quake II; requires `pics/colormap.pcx` palette; previews all mip levels)
  - SWL: built-in decoder (SiN texture; embedded palette; previews mip levels)
  - DDS: built-in decoder (uncompressed masks + BC1/BC2/BC3/BC4/BC5, including DX10 headers)

- Archive support:
  - PAK/SIN: built-in reader/writer (Quake/Quake II and SiN SPAK variant)
  - WAD: built-in reader/writer (WAD2), reader/extractor (WAD3), reader/extractor (classic Doom IWAD/PWAD including Doom 3 BFG packaged WADs), Doom-family map/BSP-lump insights (`E#M#`/`MAP##`)
  - PK3/PK4/PKZ/ZIP: built-in reader/writer via vendored miniz
  - RESOURCES (`*.resources`): built-in reader/extractor (Doom 3 BFG / DOOM 3 2019 resource container parser)
  - Quake Live Beta encrypted PK3: built-in decrypt/encrypt (XOR) loader/writer

- Model loaders:
  - MDC: built-in loader (RtCW/Enemy Territory compressed mesh)
  - MD4: built-in loader (RtCW/Enemy Territory skeletal mesh; frame-0 weighted reconstruction)
  - MDR: built-in loader (Raven skeletal mesh used by Elite Force/JK2-family games; frame-0 weighted reconstruction)
  - SKB/SKD: built-in loader (FAKK2/MOHAA/Elite Force skeletal meshes; weighted static reconstruction)
  - MDM: built-in loader (Enemy Territory skeletal mesh; companion `.mdx` skeletal data)
  - GLM: built-in loader (Ghoul2 mesh; companion `.gla` base pose when available)

- Cinematics (built-in decoders; used for thumbnails + playback widget):
  - CIN: id Quake II cinematic (PAL8 + optional PCM audio)
  - ROQ: id RoQ (vector quantized video + optional RoQ DPCM audio)

- idTech asset inspectors (built-in metadata parsers):
  - SPR: Quake/idTech2 sprite decode + animated preview + metadata/frame table summary
  - SP2 / SPR2: Quake II sprite frame references resolve to animated preview + metadata/frame table summary
  - DM2: Quake II demo packet stream summary
  - AAS: Quake III bot navigation header + lump summary
  - QVM: Quake III VM bytecode header + segment summary
  - progs.dat: QuakeC VM bytecode header + section/function-table summary
  - TAG / MDX / MDS: RtCW/Enemy Territory tag tables + skeletal metadata summaries
  - SKB / SKD: FAKK2/MOHAA skeletal mesh header + surface-table summary
  - SKC / SKA: FAKK2/MOHAA skeletal animation header + frame-layout summary

- Font inspectors:
  - TTF / OTF: built-in Insights specimen preview + family/style metadata (Qt font engine via in-memory font loading)

- Text/script preview:
  - Includes Quake/idTech script/config assets such as `cfg`, `config`, `rc`, `arena`, `bot`, `skin`, `shaderlist`, `shader`, `menu`, `vdf`, `tik`,
    `anim`, `cam`, `camera`, `char`, `voice`, `gui`, `bgui`, `efx`, `guide`, `lipsync`, `viseme`, `def`, `mtr`, `sndshd`, `af`, `pd`, `decl`, `ent`,
    `map`, `qc`, `sab`, `siege`, `veh`, `npc`, `jts`, `bset`, `weap`, `ammo`, and `campaign`
