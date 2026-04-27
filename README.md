# PakFu

<p align="center">
  <a href="VERSION"><img alt="Version" src="https://img.shields.io/badge/version-0.1.18.1-0A66C2?style=for-the-badge"></a>
  <a href="#tech-stack"><img alt="Tech Stack" src="https://img.shields.io/badge/stack-C%2B%2B20%20%7C%20Qt6%20Widgets-00599C?style=for-the-badge"></a>
  <a href="#build-and-run"><img alt="Build" src="https://img.shields.io/badge/build-Meson%20%2B%20Ninja-4C8EDA?style=for-the-badge"></a>
  <a href="#overview"><img alt="Platforms" src="https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-444444?style=for-the-badge"></a>
  <a href="#cli-quick-reference"><img alt="Interface" src="https://img.shields.io/badge/interface-GUI%20%2B%20CLI-1F6FEB?style=for-the-badge"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPLv3-2EA44F?style=for-the-badge"></a>
  <a href="https://github.com/themuffinator/PakFu/actions/workflows/pr-ci.yml"><img alt="PR CI" src="https://img.shields.io/github/actions/workflow/status/themuffinator/PakFu/pr-ci.yml?label=PR%20CI&style=for-the-badge"></a>
  <a href="https://github.com/themuffinator/PakFu/actions/workflows/nightly.yml"><img alt="Nightly" src="https://img.shields.io/github/actions/workflow/status/themuffinator/PakFu/nightly.yml?label=nightly&style=for-the-badge"></a>
  <a href="CHANGELOG.md"><img alt="Changelog" src="https://img.shields.io/badge/status-active%20development-CF8E1D?style=for-the-badge"></a>
</p>

<p align="center">
  <a href="https://github.com/themuffinator/PakFu/releases"><img alt="Download" src="https://img.shields.io/badge/download-GitHub%20Releases-2EA44F?style=for-the-badge"></a>
  <a href="docs/DEPENDENCIES.md"><img alt="Dependencies" src="https://img.shields.io/badge/docs-Dependencies-444444?style=for-the-badge"></a>
  <a href="docs/RELEASES.md"><img alt="Release Policy" src="https://img.shields.io/badge/docs-Release%20Policy-444444?style=for-the-badge"></a>
</p>

<p align="center">
  <img alt="PakFu logo" src="assets/img/logo.png" width="420" />
</p>

Step into the PakFu dojo: your Sensei demands tidy archives, sharp tools, and workflows you can repeat without hesitation. Respect the palettes, mount containers within containers, and keep your mod workbench disciplined. Train with the GUI for comfort, or the CLI for speed.

PakFu is a cross-platform archive manager and asset viewer for idTech-era and adjacent game formats. It is built with modern C++ and Qt6, and it keeps both GUI and CLI workflows first-class.

<details>
  <summary><strong>Table of Contents</strong></summary>

- [Overview](#overview)
- [Download](#download)
- [Highlights](#highlights)
- [Supported Formats](#supported-formats)
- [Build and Run](#build-and-run)
- [CLI Quick Reference](#cli-quick-reference)
- [GUI Workflow](#gui-workflow)
- [Installations (Game Profiles)](#installations-game-profiles)
- [Updates and Releases](#updates-and-releases)
- [Environment Variables](#environment-variables)
- [Documentation](#documentation)
- [Credits](#credits)
- [Tech Stack](#tech-stack)
- [License](#license)

</details>

## Overview
- Current version: `0.1.18.1` (see `VERSION`).
- Cross-platform targets: Windows, macOS, Linux.
- Build system: Meson + Ninja.
- UI framework: Qt6 Widgets.
- Primary focus: browse, inspect, preview, extract, and rebuild archive content with a documented CLI.
- Project state: active development with frequent feature updates.

## Download
Get PakFu from GitHub Releases:
- Windows: `installer.msi` (recommended) or `portable.zip`
- macOS: `installer.pkg` (recommended) or `portable.zip`
- Linux: `installer.AppImage` (recommended) or `portable.tar.gz`

The Linux portable archive is a deployed runtime tree with Qt and non-Qt
runtime libraries bundled. After extracting it, run `./pakfu` from the extracted
directory.

PakFu can also check for updates at runtime (GUI) and via CLI (`--check-updates`).
Every installer and portable package includes an offline HTML user guide in a
`Documentation` folder (or the platform's app documentation location). The guide
is generated from the user-facing sections of this README, so packaged help stays
aligned without carrying build instructions or repository maintenance notes.

## Highlights
- Archive and folder support in both GUI and CLI.
- Dedicated standalone viewer windows for image, video, audio, and model files.
- Nested container mounting (open archives inside archives).
- One-click `Extract Selected` and `Extract All` workflows for the active archive tab.
- Batch conversion tool for selected assets with category tabs (images, video, archives, models, sound, maps, text, other).
- Manifest-driven extension commands with capability negotiation and GUI import/write-back outputs.
- An installable `pakfu_core` static library with public headers and pkg-config metadata for non-UI archive, format, search, game-profile, and extension code.
- Image batch conversion supports input from all supported image preview formats, including Heretic II `m8` textures, and output to all supported image-writer formats (`png`, `jpg`, `jpeg`, `bmp`, `gif`, `tga`, `tif`, `tiff`, `pcx`, `wal`, `swl`, `mip`, `lmp`, `ftx`, `dds`) with format-aware settings (quality, compression, palette source, dithering, alpha threshold, embedded palette where applicable).
- Preview overviews surface asset context such as palette provenance, model companion files, shader texture dependency resolution, active 3D renderer state, and preview fallback notes.
- Heavy previews report timing profiles, reuse cached temp exports for repeated media/model previews, and cap BSP texture resolution work for very large maps.
- Modern platform-native open/save/folder dialogs with standard breadcrumbs/bookmarks and robust cross-platform behavior.
- 3D preview renderer selection with Vulkan/OpenGL behavior and fallback.
- Fly camera controls for 3D preview (`Right Mouse + WASD`, `Q/E`, mouse wheel speed, `Shift` faster, `Ctrl` slower, `F` frame, `R`/`Home` reset).
- Auto-detection and management of per-game installation profiles.
- Built-in update checks via GitHub Releases.
- Integrated crash reporting with session logs and Windows minidumps.
- Cross-platform shell integration: Windows Open With registration, Linux desktop/MIME metadata, macOS document types, and per-format icon sets.

## Supported Formats

For the fixture-backed support contract and current verification status, see
[`docs/SUPPORT_MATRIX.md`](docs/SUPPORT_MATRIX.md). The tables below are the
quick reference.

### Archive And Container Support
| Type | Extensions | Open/List | Extract | Save/Rebuild |
|---|---|---|---|---|
| Folder input | directory path | Yes | Yes | N/A |
| PAK/SIN | `pak`, `sin` | Yes | Yes | Yes |
| ZIP family | `zip`, `pk3`, `pk4`, `pkz` | Yes | Yes | Yes |
| Quake Live Beta encrypted PK3 | `pk3` (XOR-obfuscated) | Yes (auto-detect/decode) | Yes | Yes (encode/decode via Save As) |
| Doom 3 BFG resources | `resources` | Yes | Yes | No |
| WAD2 | `wad`, `wad2` | Yes | Yes | Yes |
| WAD3 | `wad3` | Yes | Yes | No |
| Doom IWAD/PWAD | `wad` | Yes | Yes | No |

### Preview And Inspector Support
- Images:
  - Core: `pcx`, `wal`, `png`, `tga`, `jpg`
  - Also supported: `jpeg`, `bmp`, `gif`, `tif`, `tiff`, `swl`, `m8`, `mip`, `lmp`, `dds`, `ftx`
- Audio:
  - Core: `wav`, `ogg`
  - Also supported: `mp3`, `idwav` (Doom 3 BFG; converted to WAV for playback when payload codec is supported)
- Video:
  - Core: `cin`, `roq`
  - Also supported: `bik`, `ogv`, `mp4`, `mkv`, `avi`, `webm`
- Models:
  - `mdl`, `md2`, `fm`, `md3`, `mdc`, `md4`, `mdr`, `skb`, `skd`, `mdm`, `glm`, `iqm`, `md5mesh`, `tan`, `obj`, `lwo`
- idTech inspectors and metadata views:
  - `spr`, `sp2`/`spr2`, `bk`, `os`, `dm2`, `aas`, `qvm`, `progs.dat`, `tag`, `mdx`, `mds`, `skc`, `ska`, `ttf`, `otf`
- Text and script assets:
  - Core: `cfg` and similar plain-text config/script files
  - Includes many common idTech-family script formats (`shader`, `menu`, `def`, `mtr`, `map`, `proc`, `ent`, `qc`, and others)

Notes:
- Multimedia playback support depends on the installed Qt Multimedia backend and codecs.
- `cin` and `roq` are also handled by built-in cinematic decoders.
- Some indexed formats (`wal`, `mip`, selected `lmp` cases) use game palettes when required; Heretic II `m8` textures carry an embedded palette.
- Heretic II `bk` book sprites composite referenced `.m8` tiles, while `os` dynamic scripts are binary bytecode and open as metadata/opcode previews.
- BSP inspector/preview supports Heretic II `IBSP`/converted `QBSP` maps with `.m8` texture resolution, plus Quake 3-derived families including FAKK variants used by Heavy Metal: F.A.K.K.2 and American McGee's Alice (`FAKK` v42 checksum-header BSPs).
- idTech4 `.map` source files and `.proc` compiled render descriptions open as text/metadata inspections. 3D map rendering is currently scoped to Quake-family `.bsp` files; idTech4 `.proc`/collision map rendering is not claimed.

## Build and Run

### Prerequisites
- C++20 toolchain
- Meson + Ninja
- Qt6 (Core, Gui, Network, Widgets, OpenGL, OpenGLWidgets, Multimedia, MultimediaWidgets)
- Windows: `DbgHelp` (system library, used for crash reporting)

The Vulkan 3D preview renderer is optional at build time. If Qt private RHI
headers are unavailable, Meson builds the OpenGL preview path instead; use
`-Dvulkan_preview=enabled` to require Vulkan or `-Dvulkan_preview=disabled` to
force the portable OpenGL-only build.

See `docs/DEPENDENCIES.md` for full dependency details and packaging toolchain requirements.

### Windows Helper Build
```powershell
pwsh -NoProfile -File scripts/meson_build.ps1
```

### Manual Build (All Platforms)
```sh
meson setup builddir --backend ninja
meson compile -C builddir
```

### Core Library Target
```sh
meson compile -C builddir pakfu_core
meson test -C builddir core-api-smoke
```

Installed builds publish `pakfu_core`, the public header set under
`include/pakfu`, and `pakfu_core.pc` metadata for helper tools that want to reuse
PakFu's non-UI archive and asset-format surface.

### Parser Hardening Build
For parser hardening smoke tests with libFuzzer + sanitizers:
```sh
CC=clang CXX=clang++ meson setup builddir-fuzz --backend ninja --buildtype=debugoptimized -Dvulkan_preview=disabled -Dfuzzing=enabled -Db_sanitize=address,undefined
meson test -C builddir-fuzz --print-errorlogs
```

### Run
```sh
./builddir/src/pakfu
./builddir/src/pakfu --cli --help
```

Windows:
```powershell
.\builddir\src\pakfu.exe
.\builddir\src\pakfu.exe --cli --help
```

## CLI Quick Reference
Usage:
```text
pakfu[.exe] --cli [options] [archive-or-folder]
```

Core actions:
- `-l, --list` : list entries.
- `-i, --info` : show archive summary.
- `-x, --extract` : extract entries (`-o, --output <dir>` optional).
- `--save-as <archive>` : rebuild selected entries into a new archive.
- `--convert <format>` : convert selected images to a supported image-writer output (`png`, `jpg`, `bmp`, `gif`, `tga`, `tiff`, `pcx`, `wal`, `swl`, `mip`, `lmp`, `ftx`, `dds`) or IDWAV audio to `wav`.
- `--preview-export <entry>` : export the CLI preview rendition of one entry; images write an image file, text and binary entries write bytes.
- `--list-plugins` : list discovered extension commands.
- `--run-plugin <plugin[:command]>` : run one extension command against the loaded archive.
- `--check-updates` : query GitHub Releases.
- `--qa-practical` : run practical archive-ops smoke QA (selection/marquee/modifier checks).

Archive selection and mounting:
- `--entry <path>` : limit `--list`, `--extract`, `--save-as`, `--convert`, or `--run-plugin` to an exact archive entry. Repeat for multiple entries.
- `--prefix <dir>` : limit `--list`, `--extract`, `--save-as`, `--convert`, or `--run-plugin` to entries under a directory prefix. Repeat for multiple prefixes.
- `--mount <entry>` : mount a nested archive entry first, then run the requested action against the mounted archive.
- `--plugin-dir <dir>` : add an extension search directory (repeat for more than one).

Save-as options:
- `--format <pak|sin|zip|pk3|pk4|pkz|wad|wad2>` : choose the output archive format. If omitted, PakFu infers it from `--save-as`.
- `--quakelive-encrypt-pk3` : write ZIP-family `--save-as` output with Quake Live Beta PK3 encryption.

Installation profile actions:
- `--list-game-installs`
- `--auto-detect-game-installs`
- `--select-game-install <selector>`

Legacy aliases still accepted:
- `--list-game-sets`
- `--auto-detect-game-sets`
- `--select-game-set <selector>`

Update source overrides:
- `--update-repo <owner/name>`
- `--update-channel <stable|beta|dev>` (full releases only; prereleases are ignored)

Examples:
```sh
./builddir/src/pakfu --cli --info path/to/archive.pk3
./builddir/src/pakfu --cli --list path/to/archive.wad
./builddir/src/pakfu --cli --extract -o out_dir path/to/archive.resources
./builddir/src/pakfu --cli --extract --prefix maps -o maps_out path/to/archive.pk3
./builddir/src/pakfu --cli --save-as mod_subset.pk3 --format pk3 --prefix textures path/to/archive.pk3
./builddir/src/pakfu --cli --convert png --prefix textures -o converted_textures path/to/archive.pk3
./builddir/src/pakfu --cli --preview-export gfx/conback.lmp -o previews path/to/pak0.pak
./builddir/src/pakfu --cli --mount nested/maps.pk3 --list path/to/archive.pk3
./builddir/src/pakfu --cli --list-plugins
./builddir/src/pakfu --cli --run-plugin example:inspect-cfg --entry scripts/autoexec.cfg path/to/archive.pk3
./builddir/src/pakfu --cli --check-updates
./builddir/src/pakfu --cli --qa-practical
./builddir/src/pakfu --cli --list-game-installs
./builddir/src/pakfu --cli --auto-detect-game-installs
./builddir/src/pakfu --cli --select-game-install quake2
```

## GUI Workflow
- Main modes:
  - Archive View
  - Image Viewer
  - Video Viewer
  - Audio Viewer
  - Model Viewer
- Opening behavior:
  - Opening a supported media/model file can launch its dedicated viewer window.
  - Opening an archive prompts: open directly, install a copy then open, or move to installation then open.
- Navigation:
  - In standalone viewers, mouse wheel and arrow keys cycle sibling files in the same folder.
  - Fullscreen toggles with `F11`, middle mouse, or platform fullscreen shortcut.
- Archive operations:
  - Drag-and-drop import/export across directories and tabs, plus file/folder drops from external applications.
  - Drag modifier keys: hold `Shift` to request move; hold `Ctrl/Cmd` (or `Option` on macOS) to force copy. Cross-application drops are copy.
  - Move semantics work across open tabs via drag or cut/paste when the source tab is editable; otherwise operations safely fall back to copy.
  - Locked/read-only archive states (Pure PAK protector, mounted container views, folder views) disallow import drops and disable cut/paste-style modification actions.
  - Name collisions during add/import/rename prompt for `Overwrite`, `Keep Both`, `Skip`, or `Cancel` (with apply-to-remaining support).
  - Multi-selection follows platform conventions: `Shift` range selection, `Ctrl`/`Cmd` toggle selection, marquee/rubber-band selection in icon/list views, and `Ctrl`/`Cmd`+`A` for Select All.
  - Shell-open paths from Explorer/Finder/file managers route through the same open flow as drag/drop, including directories, archive tabs, and standalone viewers.
  - Double-click nested container files to mount and browse in-place.
  - Multiple nested layers are supported.
  - `File` menu and context menu actions support `Extract Selected`, `Extract All` (archive views), and `Convert Selected Assets...`.
  - The `Extensions` menu discovers manifest-backed external tools, validates the current archive selection before launch, and can import extension-generated files as pending archive changes.
- Safety:
  - Pure PAK Protector is enabled by default for official archives (read-only protection with Save As workflow).

## Installations (Game Profiles)
PakFu stores per-game installation profiles with:
- default directories,
- palette preferences,
- launch settings (executable/arguments/working directory).

Behavior:
- If profiles exist, PakFu opens directly into Archive View.
- If none exist, the Installations dialog opens on startup.
- Auto-detection order: Steam -> GOG.com -> EOS.
- Auto-detection coverage includes Quake-family, Doom-family, Half-Life, SiN, RtCW/ET, Jedi Outcast/Academy, Elite Force titles, Heavy Metal: F.A.K.K.2, American McGee's Alice, Quake 4, Doom 3, Doom 3 BFG Edition, Prey, and Enemy Territory: Quake Wars.

Selector support:
- `--select-game-install` accepts UID, game key, or display name.
- Use `--list-game-installs` to enumerate current profiles and keys.

## Updates and Releases
PakFu can check GitHub Releases at runtime and from CLI.
GUI auto-checks run after the main window is shown, so network latency does not block startup.

GUI updater install handoff:
- Downloaded update assets are verified against the release manifest (`size` and `sha256`) before PakFu opens the download folder or launches an installer.
- Windows: `Install and Restart` waits for installer completion, then relaunches PakFu (preferring the installer-managed `%LOCALAPPDATA%\PakFu\pakfu.exe` after MSI updates).
- macOS/Linux: PakFu exits and starts the downloaded installer artifact (`.pkg` / `.AppImage`) after shutdown.

Build-time updater config:
- `-Dgithub_repo=themuffinator/PakFu` (default; see `meson_options.txt`)
- `-Dupdate_channel=stable|beta|dev` (default: `stable`; full releases only; see `meson_options.txt`)

Release automation:
- Nightly pipeline: `.github/workflows/nightly.yml`
- Channel auto-version pipeline: `.github/workflows/auto_version.yml`
- Manual rebuild pipeline: `.github/workflows/release.yml`

Release artifact naming contract:
- `pakfu-<version>-windows-<arch>-installer.msi`
- `pakfu-<version>-windows-<arch>-portable.zip`
- `pakfu-<version>-macos-<arch>-installer.pkg`
- `pakfu-<version>-macos-<arch>-portable.zip`
- `pakfu-<version>-linux-<arch>-installer.AppImage`
- `pakfu-<version>-linux-<arch>-portable.tar.gz`

For full policy details, see `docs/RELEASES.md`.

## Environment Variables
| Variable | Purpose |
|---|---|
| `PAKFU_CRASH_DIR` | Override crash output directory (session logs and crash artifacts). |
| `PAKFU_DISABLE_QT_MESSAGE_HOOK` | Disable Qt log interception for troubleshooting. |
| `PAKFU_DEBUG_MEDIA` | Enable extra media diagnostics in logs. |
| `PAKFU_AUTO_PLAY_ON_OPEN` | Auto-start playback when opening videos. |
| `PAKFU_FORCE_QT_FILE_DIALOG` | Force Qt's non-native file dialog implementation (useful for troubleshooting platform dialog issues). |
| `PAKFU_ALLOW_MULTI_INSTANCE` | Disable single-instance behavior and allow multiple app instances. |
| `PAKFU_SMOKE_TABS` | Run tab smoke test automation on startup (debug/CI helper). |
| `QT_MEDIA_BACKEND` | Override Qt multimedia backend selection. |

## Documentation
- `docs/CORE_LIBRARY.md` : `pakfu_core` public headers, API metadata, install contract, and sample consumer code.
- `docs/DEPENDENCIES.md` : dependency baseline and optional libraries.
- `docs/EXTENSIONS.md` : manifest schema, JSON payload contract, and CLI/GUI extension workflow.
- `docs/QA_ARCHIVE_FILE_OPS.md` : practical QA checklist for selection, drag/drop, cut/copy/paste, and modifiers.
- `docs/RELEASES.md` : versioning, release rules, and asset contract.
- `docs/UI_BUTTON_ICONS.md` : UI action icon inventory.
- `docs/FILE_ASSOCIATION_ICONS.md` : file association icon mapping and implementation notes.
- `docs/CREDITS.md` : project attributions, compatibility acknowledgements, and third-party credits.
- `CHANGELOG.md` : release-by-release change history.
- `scripts/build_user_guide.py` : generates the packaged HTML user guide from the user-facing README sections.

## Credits
- Creator: themuffinator (DarkMatter Productions)
- Full attribution list: `docs/CREDITS.md`

## Tech Stack
- Language: C++20
- UI: Qt6 Widgets
- Build: Meson + Ninja
- Rendering preview path: Vulkan and OpenGL paths with fallback behavior
- Packaging and release automation: platform scripts + GitHub Actions

## License
- GPLv3 (`LICENSE`)
- No warranty
