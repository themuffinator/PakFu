# PakFu 🥋📦🔥

Bow to your Sensei! 🙇‍♂️🙇‍♀️🧎‍➡️🧎‍➡️🧎‍➡️  
PakFu is a modern, cross-platform PAK/WAD file manager forged in the dojo of **C++20** ⚔️ and the ancient arts of **Qt6** 🐉 (Widgets or QML). It exists to bring balance to chaotic archives: browse 🧭, preview 👀, extract 🧤, rebuild 🛠️, and automate 🧙‍♂️ via CLI.

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
  - Images: `pcx`, `wal`, `mip`, `dds`, `lmp`, `png`, `tga`, `jpg` 🖼️🎨
  - Audio: `wav`, `ogg`, `mp3` 🔊🎶
  - Video: `cin`, `roq`, `ogv` 🎞️🍿
  - Models: `mdl`, `md2`, `md3`, `iqm`, `md5mesh`, `lwo`, `obj` 🧊🧩
  - Maps: `bsp` (Quake/Quake II/Quake III/Quake Live) 🗺️🧭
  - Text/config: `cfg` and similar plain-text formats 🧾🖋️
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
./build/src/pakfu --cli --info path/to/archive.pk3
./build/src/pakfu --cli --list path/to/archive.pk4
./build/src/pakfu --cli --extract -o out_dir path/to/archive.pkz
```

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

Release assets should include platform-appropriate packages 🎁 (installers preferred 🧰✅, archives supported 📦👌).

See `docs/RELEASES.md` for versioning and release automation details 🧾🤖.

## Dependencies Scroll 🧾🧪
See `docs/DEPENDENCIES.md` for the current baseline and planned format loaders 📚🔍.

## Build Ritual (Meson + Ninja) 🥷🛠️

### Windows (recommended) 🪟🥋
The dojo provides a helper script that finds Qt's `qmake6` and builds with Ninja:

```pwsh
pwsh -NoProfile -File scripts/meson_build.ps1
```

On Windows, the script also deploys the required Qt runtime DLLs into `build/src/` so `build/src/pakfu.exe` runs without needing to add Qt to `PATH`.

### Manual (all platforms) 🌍🧙‍♂️

```sh
meson setup build --backend ninja -Dgithub_repo=owner/name -Dupdate_channel=stable
meson compile -C build
```

## Run (GUI or CLI) 🏃‍♂️💨

### GUI 🪟
- Use **File → Open Archive…** or **File → Open Folder…** (opens in a tab).

### CLI 🧑‍💻
- Most archive actions also work on folders (pass a directory path instead of an archive file).

## Game Sets 🎮🧾
PakFu uses **Game Sets** to store per-game defaults:
- Default directory (for file dialogs / common workflows)
- Palette selection (for game-specific previews)
- Launch settings (executable + args)

If **Game Sets** are already configured, PakFu opens directly into the main window.
If none are configured yet, the **Game Sets** window appears on startup.
In the main window, use the **Game** drop-down to switch sets instantly, or choose **Configure Game Sets…** to edit/auto-detect.
When opening an archive, PakFu will try to auto-select the most likely Game Set based on the archive’s path and nearby install markers.
Auto-detect checks installs in priority order: **Steam → GOG.com → EOS**.

Supported auto-detect games:
- Quake
- Quake Rerelease
- Quake II
- Quake II Rerelease
- Quake III Arena
- Quake Live
- Quake 4

```sh
./build/src/pakfu
./build/src/pakfu --cli --help
./build/src/pakfu --cli --check-updates
```

On Windows, use:

```pwsh
.\build\src\pakfu.exe
.\build\src\pakfu.exe --cli --help
```

Game Sets can also be managed via CLI:

```sh
./build/src/pakfu --cli --list-game-sets
./build/src/pakfu --cli --auto-detect-game-sets
./build/src/pakfu --cli --select-game-set quake2
./build/src/pakfu --cli --select-game-set quake3_arena
./build/src/pakfu --cli --select-game-set quake_live
./build/src/pakfu --cli --select-game-set quake4
```

## License & Disclaimer ⚖️🧾
- **License**: GPLv3 📜🐧
- **Disclaimer**: Use at your own risk. No warranty. No mercy. 🥋⚠️😅

## Final Bow 🙇‍♂️🙇‍♀️
This repository is intentionally minimal right now. As features arrive, keep the dojo rules aligned with the design and build choices 🧘‍♂️📦✨.
