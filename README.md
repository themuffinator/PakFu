# PakFu 🥋📦🔥

Bow to your Sensei! 🙇‍♂️🙇‍♀️🧎‍➡️🧎‍➡️🧎‍➡️  
PakFu is a modern, cross-platform game file viewer and PAK/WAD manager forged in the dojo of **C++20** ⚔️ and the ancient arts of **Qt6** 🐉 (Widgets or QML). It exists to bring balance to chaotic archives and assets: browse 🧭, preview 👀, extract 🧤, rebuild 🛠️, and automate 🧙‍♂️ via CLI.

This project is still training under the waterfall 💦🥋 (early development). Expect rapid evolution, occasional shin bruises, and increasingly disciplined PAKs.

## The Dojo Scroll (What This Is) 🧾🥢
- **GUI**: Qt6 Widgets or QML 🪟✨
- **CLI**: first-class, scriptable, and always respected 🧑‍💻⚡
- **Targets**: Windows 🪟, macOS 🍎, Linux 🐧 (no platform favoritism in my dojo)
- **Build**: Meson + Ninja 🥷🛠️

## The Way of PakFu (Project Goals) 🐲📜
- Deliver a modern, intuitive UI that feels fast, clean, and confident 🧼⚡🧠
- Provide a first-class CLI for automation, pipelines, and batch wizardry 🪄🧰
- Run consistently on Windows/macOS/Linux 🧘‍♂️🌍
- Understand and preview common PAK-adjacent file types 👁️‍🗨️📁
  - Images: `pcx`, `wal`, `swl`, `mip`, `dds`, `lmp`, `png`, `tga`, `jpg` 🖼️🎨
  - Audio: `wav`, `ogg`, `mp3`, `bik` (audio track playback via Qt backend support) 🔊🎶
  - Video: `cin`, `roq`, `ogv`, `bik` 🎞️🍿
  - Models: `mdl` (Quake + GoldSrc/Half-Life Studio), `md2`, `md3`, `mdc`, `md4`, `mdr`, `skb`, `skd`, `mdm`, `glm`, `iqm`, `md5mesh`, `lwo`, `obj` 🧊🧩
  - Sprites: `spr`, `sp2`/`spr2` (animated sprite preview + metadata/frame table insights, including Half-Life/GoldSrc SPR v2 embedded-palette files) 🧾🎯
  - Demos: `dm2` (Quake II packet stream summary preview) 📼🧾
  - Fonts: `ttf`, `otf` (Insights specimen preview + family/style metadata) 🔤🧾
  - Maps: `bsp` (all supported Quake-family idTech1/idTech2/idTech3 titles, including GoldSrc/Half-Life, Quake BSP2/2PSB, RtCW/ET `IBSP` v47 with ET foliage surfaces, and RBSP/FBSP/FAKK/EF2 variants) + Doom-family WAD map/BSP-lump insights (`E#M#`/`MAP##`) 🗺️🧭
  - Navigation/VM: `aas`, `qvm`, `progs.dat` (header/lump/function-table summary preview) 🧠📦
  - RtCW/ET model metadata: `tag`, `mdx`, `mds` (header/table/frame/bone/tag summaries in Insights) 🐺🧾
  - FAKK2/MOHAA/EF skeletal assets: `mdr`, `skb`, `skd`, `skc`/`ska` (header/surface/frame-table insights + model preview for `mdr`/`skb`/`skd`) 🦴🧾
  - Integrity manifests: `crc` (Doom 3 BFG `.resources` checksum tables) ✅🧾
  - Text/config: `cfg`, `config`, `rc`, `arena`, `bot`, `skin`, `shaderlist`, `lang`, `lst`, `tik`, `anim`, `cam`, `camera`, `char`, `voice`, `gui`, `bgui`, `efx`, `guide`, `lipsync`, `viseme`, `vdf`, `def`, `mtr`, `sndshd`, `af`, `pd`, `decl`, `ent`, `map`, `qc`, `sab`, `siege`, `veh`, `npc`, `jts`, `bset`, `weap`, `ammo`, `campaign`, and similar plain-text formats 🧾🖋️
  - Shader scripts: `shader` opens as a tiled, script-driven Quake III preview with shader-block selection copy/paste append editing (including animated maps, tcMods, and deform directives) 🧪🧱
- Tune 3D previews with grid/floor/none options, themed/grey/custom backgrounds, and wireframe/textured toggles
- 3D navigation: hold Right Mouse to fly (WASD/QE, wheel adjusts speed, Shift faster, Ctrl slower); F frames, R/Home resets; Grid mode includes a 32x32x56 player-scale reference box
- Quake II previews honor `_glow.png` glow maps for textures and models
- WAD support: read/extract `WAD2`, `WAD3`, and classic Doom `IWAD`/`PWAD` (including Doom-family map/BSP-lump insights); rebuild/write `WAD2`
- Stay responsive for large archives (no freezing in the middle of a roundhouse kick) 🥶➡️🥋
- Guard official game archives with a default-on Pure PAK Protector preference 🛡️📦
- Keep C++ code clean, portable, and documented (minimal OS-specific sorcery) 🧠🧹🧾

## PK3 / PK4 / PKZ Support (ZIP packs) 📦🧨
PakFu treats `*.pk3`, `*.pk4`, and `*.pkz` as **ZIP-based packs** (same container, different extension).

Common conventions:
- `*.pk3`: id Tech 3 packs (Quake III Arena / Quake Live, etc.)
- `*.pk4`: id Tech 4 packs (Doom 3 / Quake 4, etc.)
- `*.pkz`: ZIP-based packs used by some games/mods (handled like PK3/ZIP)

These packs are supported anywhere PakFu supports ZIP:

- **Open/browse/preview/extract** in the GUI
- **List/info/extract** in the CLI (`--cli`)
- **Rebuild/write** via **File → Save / Save As…** (uses a vendored `miniz` backend)

```sh
./builddir/src/pakfu --cli --info path/to/archive.pk3
./builddir/src/pakfu --cli --list path/to/archive.pk4
./builddir/src/pakfu --cli --extract -o out_dir path/to/archive.pkz
```

## Doom 3 BFG `.resources` Support 📚
PakFu supports Doom 3 BFG/DOOM 3 (2019) `*.resources` containers with native parsing:

- **Open/browse/preview/extract** in the GUI
- **List/info/extract** in the CLI (`--cli`)
- **Save/rebuild/write** is not supported yet for `.resources`

### Quake Live Beta (QL BETA) encrypted PK3 (encode/decode) 🔐
Quake Live **Beta** used an XOR-obfuscated PK3. PakFu can transparently **decode** these when reading, and can **encode** them when writing:

- **Auto-detect on open**: if a `*.pk3` looks like a Quake Live Beta encrypted ZIP header, PakFu decodes it to a temporary ZIP for reading/listing/extraction.
  - CLI tip: `--cli --info` prints `Quake Live encrypted PK3: yes` when detected.
- **Decode (encrypted → normal PK3)**: open the encrypted `*.pk3`, then **File → Save As… → `PK3 (ZIP) (*.pk3)`**
- **Encode (normal → QL Beta encrypted PK3)**: open any ZIP-based pack, then **File → Save As… → `PK3 (Quake Live encrypted) (*.pk3)`**
- **Note**: encode/decode is the same XOR stream operation (historical obfuscation, not real security).

## The Forbidden Techniques (Non-Goals... for now) 🙅‍♂️⛔
- Deep editing of complex proprietary binary formats 🧟‍♂️📦
- Bundling game-specific proprietary tooling 🕳️🔒
- Platform-exclusive UI tricks that break cross-platform harmony 🧨💥

## Sacred Rules of the Dojo (Product Rules) 📜🗿
- UI must be **Qt6 Widgets or QML**. No alternate frameworks. Ever. 🛑🪟
- Must remain cross-platform; avoid OS-locked dependencies 🧘‍♀️🌐
- CLI remains supported and documented 🧾🧑‍💻
- File format support must be modular and additive 🧩➕
- Docs must evolve with behavior (a silent Sensei is a bad Sensei) 📣📚

## The Toolbelt (Tech Stack) 🧰🧲
- C++ (modern, prefer C++20) ⚔️
- Qt6 (Widgets/QML) 🐉
- Meson + Ninja 🥷🛠️

## The Update Ritual (GitHub Releases) 🔄📦
PakFu checks GitHub Releases for new scrolls (updates) 🧾✨. Configure the repo at Meson setup time so the updater knows which mountain to climb 🏔️🐙:

- `-Dgithub_repo=owner/name` 🧭
- `-Dupdate_channel=stable|beta|dev` 🧪

Nightly releases are automated (scheduled + commit-gated) and publish both installer + portable artifacts per platform.
Use canonical asset names so updater selection stays deterministic:
- `pakfu-<version>-windows-<arch>-installer.msi`
- `pakfu-<version>-windows-<arch>-portable.zip`
- `pakfu-<version>-macos-<arch>-installer.pkg`
- `pakfu-<version>-macos-<arch>-portable.zip`
- `pakfu-<version>-linux-<arch>-installer.AppImage`
- `pakfu-<version>-linux-<arch>-portable.tar.gz`

See `docs/RELEASES.md` for versioning and release automation details 🧾🤖.

## Dependencies Scroll 🧾🧪
See `docs/DEPENDENCIES.md` for the current baseline and planned format loaders 📚🔍.

## UI Icon Map 🖼️
See `docs/UI_BUTTON_ICONS.md` for the button/action icon inventory and SVG asset mapping.

## File Association Icons 🧷
PakFu now defines a full per-extension file-association icon set for archive, image, video, audio, and model types:
archives (`.pak`, `.sin`, `.pk3`, `.pk4`, `.pkz`, `.zip`, `.resources`, `.wad`, `.wad2`, `.wad3`), images (`.pcx`, `.wal`, `.swl`, `.mip`, `.lmp`, `.dds`, `.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`, `.gif`, `.tif`, `.tiff`), videos (`.cin`, `.roq`, `.ogv`, `.bik`, `.mp4`, `.mkv`, `.avi`, `.webm`), audio (`.wav`, `.ogg`, `.mp3`), and models (`.mdl`, `.md2`, `.md3`, `.mdc`, `.md4`, `.mdr`, `.skb`, `.skd`, `.mdm`, `.glm`, `.iqm`, `.md5mesh`, `.lwo`, `.obj`).

- On Windows: icons are generated with extension text + unique colors and registered per-extension in the user registry (`HKCU`).
- On macOS/Linux: file associations remain installer-managed, but the same extension set is documented for packaging parity.
- Manage per-format association state from **File → File Associations...** (tabbed **Archives** / **Images** / **Videos** / **Audio** / **Models**).

See `docs/FILE_ASSOCIATION_ICONS.md` for the extension/color map and implementation notes.

## Build Ritual (Meson + Ninja) 🥷🛠️

### Windows (recommended) 🪟🥋
The dojo provides a helper script that finds Qt's `qmake6` and builds with Ninja:

```pwsh
pwsh -NoProfile -File scripts/meson_build.ps1
```

On Windows, the script also deploys the required Qt runtime DLLs into `builddir/src/` so `builddir/src/pakfu.exe` runs without needing to add Qt to `PATH`.

### Manual (all platforms) 🌍🧙‍♂️

```sh
meson setup builddir --backend ninja -Dgithub_repo=owner/name -Dupdate_channel=stable
meson compile -C builddir
```

## Run (GUI or CLI) 🏃‍♂️💨

### GUI 🪟
- PakFu now has five GUI modes: **Archive View** (main window), **Image Viewer**, **Video Viewer**, **Audio Viewer**, and **Model Viewer** (standalone, read-only viewer windows).
- Use **File → Open File…** for direct file viewing. Supported images/videos/audio/models open in their standalone viewer windows; other file types still open in **Archive View** Insights.
- Opening a supported image/video/audio/model file directly with PakFu from your OS goes straight to its dedicated viewer mode.
- In **Image Viewer**, **Video Viewer**, **Audio Viewer**, and **Model Viewer**, use mouse wheel or Left/Right arrows to cycle sibling files in the same folder.
- Fullscreen toggles for viewer windows: Middle Mouse, `F11`, or platform fullscreen shortcuts.
- Opening an archive from disk now first offers: **Open directly**, **Install a copy then open**, or **Move to installation then open**.
- Drag archives or folders onto the window to open them.
- Drag regular files onto Archive View to open them in the appropriate viewer mode.
- Double-click regular files in Archive View to export and open them with the OS-associated app.
- Double-click nested container files (`wad`, `wad2`, `wad3`, `pak`, `sin`, `pk3`, `pk4`, `pkz`, `zip`, `resources`) to open them in-place.
- Nested container mounting supports multiple layers (you can keep drilling into archives inside archives).
- Drag files/folders into an open archive tab to add them.
- Drag items out of PakFu to copy them to your file manager (exports via temp files).
- Integrated crash reporting writes session logs plus Windows minidumps to:
  - `%LOCALAPPDATA%\\PakFu\\PakFu\\crashes` by default
  - override with `PAKFU_CRASH_DIR=<path>` when needed
  - disable Qt log interception with `PAKFU_DISABLE_QT_MESSAGE_HOOK=1` (troubleshooting only)
  - media diagnostics: `PAKFU_DEBUG_MEDIA=1` and `PAKFU_AUTO_PLAY_ON_OPEN=1`

### CLI 🧑‍💻
- Most archive actions also work on folders (pass a directory path instead of an archive file).

## Game Sets 🎮🧾
PakFu uses **Game Sets** to store per-game defaults:
- Default directory (for file dialogs / common workflows)
- Palette selection (for game-specific previews)
- Launch settings (executable + args)

If **Game Sets** are already configured, PakFu opens directly into **Archive View**.
If none are configured yet, the **Game Sets** window appears on startup.
In **Archive View**, use the **Game** drop-down to switch sets instantly, or choose **Configure Game Sets…** to edit/auto-detect.
When opening an archive, PakFu will try to auto-select the most likely Game Set based on the archive’s path and nearby install markers.
Auto-detect checks installs in priority order: **Steam → GOG.com → EOS**.

Supported auto-detect games:
- Anachronox
- Daikatana
- DOOM
- DOOM II
- Doom 3
- Doom 3: BFG Edition
- Enemy Territory: Quake Wars
- Final DOOM
- Gravity Bone
- Half-Life
- Heavy Metal: F.A.K.K.2
- Heretic
- Heretic II
- Hexen
- Kingpin: Life of Crime
- Prey
- Quake
- Quake 4
- Quake II
- Quake II RTX
- Quake II Rerelease
- Quake III Arena
- Quake Live
- Quake Rerelease
- Return to Castle Wolfenstein
- SiN Gold
- Star Trek Voyager: Elite Force
- Star Trek: Elite Force II
- Star Wars Jedi Knight II: Jedi Outcast
- Star Wars Jedi Knight: Jedi Academy
- Strife
- Thirty Flights of Loving
- Warfork
- Warsow
- Wolfenstein: Enemy Territory
- World of Padman

```sh
./builddir/src/pakfu
./builddir/src/pakfu --cli --help
./builddir/src/pakfu --cli --check-updates
```

On Windows, use:

```pwsh
.\builddir\src\pakfu.exe
.\builddir\src\pakfu.exe --cli --help
```

Game Sets can also be managed via CLI:

```sh
./builddir/src/pakfu --cli --list-game-sets
./builddir/src/pakfu --cli --auto-detect-game-sets
./builddir/src/pakfu --cli --select-game-set quake2
./builddir/src/pakfu --cli --select-game-set quake2_rtx
./builddir/src/pakfu --cli --select-game-set half_life
./builddir/src/pakfu --cli --select-game-set doom
./builddir/src/pakfu --cli --select-game-set doom2
./builddir/src/pakfu --cli --select-game-set final_doom
./builddir/src/pakfu --cli --select-game-set heretic
./builddir/src/pakfu --cli --select-game-set hexen
./builddir/src/pakfu --cli --select-game-set strife
./builddir/src/pakfu --cli --select-game-set quake3_arena
./builddir/src/pakfu --cli --select-game-set quake_live
./builddir/src/pakfu --cli --select-game-set quake4
./builddir/src/pakfu --cli --select-game-set doom3
./builddir/src/pakfu --cli --select-game-set doom3_bfg_edition
./builddir/src/pakfu --cli --select-game-set jedi_academy
./builddir/src/pakfu --cli --select-game-set daikatana
./builddir/src/pakfu --cli --select-game-set anachronox
./builddir/src/pakfu --cli --select-game-set heretic2
./builddir/src/pakfu --cli --select-game-set elite_force2
./builddir/src/pakfu --cli --select-game-set warfork
./builddir/src/pakfu --cli --select-game-set warsow
./builddir/src/pakfu --cli --select-game-set world_of_padman
./builddir/src/pakfu --cli --select-game-set heavy_metal_fakk2
```

## License & Disclaimer ⚖️🧾
- **License**: GPLv3 📜🐧
- **Disclaimer**: Use at your own risk. No warranty. No mercy. 🥋⚠️😅

## Final Bow 🙇‍♂️🙇‍♀️
This repository is intentionally minimal right now. As features arrive, keep the dojo rules aligned with the design and build choices 🧘‍♂️📦✨.
