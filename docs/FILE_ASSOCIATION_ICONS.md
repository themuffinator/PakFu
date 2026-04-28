# File Association Icons

PakFu uses a per-extension icon set for managed archive, image, video, audio, model, and asset associations so each file type is visually distinct and readable in file managers.

## Managed extensions

| Category | Extension | Color |
| --- | --- | --- |
| Archive | `.pak` | `#D35400` |
| Archive | `.sin` | `#8E6E53` |
| Archive | `.pk3` | `#1E88E5` |
| Archive | `.pk4` | `#3949AB` |
| Archive | `.pkz` | `#00897B` |
| Archive | `.zip` | `#43A047` |
| Archive | `.resources` | `#6D4C41` |
| Archive | `.wad` | `#8E24AA` |
| Archive | `.wad2` | `#F4511E` |
| Archive | `.wad3` | `#00838F` |
| Asset | `.bsp` | `#7E57C2` |
| Asset | `.map` | `#5E35B1` |
| Asset | `.proc` | `#3949AB` |
| Image | `.pcx` | `#546E7A` |
| Image | `.wal` | `#1565C0` |
| Image | `.swl` | `#2E7D32` |
| Image | `.m8` | `#7B1FA2` |
| Image | `.m32` | `#8E24AA` |
| Image | `.mip` | `#6A1B9A` |
| Image | `.lmp` | `#5D4037` |
| Image | `.dds` | `#0277BD` |
| Image | `.png` | `#00ACC1` |
| Image | `.jpg` | `#F9A825` |
| Image | `.jpeg` | `#F57F17` |
| Image | `.tga` | `#7CB342` |
| Image | `.bmp` | `#8D6E63` |
| Image | `.gif` | `#EC407A` |
| Image | `.tif` | `#455A64` |
| Image | `.tiff` | `#37474F` |
| Video | `.cin` | `#5E35B1` |
| Video | `.roq` | `#3949AB` |
| Video | `.ogv` | `#039BE5` |
| Video | `.bik` | `#00897B` |
| Video | `.mp4` | `#1E88E5` |
| Video | `.mkv` | `#7CB342` |
| Video | `.avi` | `#6D4C41` |
| Video | `.webm` | `#00ACC1` |
| Audio | `.wav` | `#43A047` |
| Audio | `.idwav` | `#66BB6A` |
| Audio | `.ogg` | `#26A69A` |
| Audio | `.mp3` | `#F9A825` |
| Model | `.mdl` | `#8D6E63` |
| Model | `.md2` | `#5C6BC0` |
| Model | `.fm` | `#512DA8` |
| Model | `.md3` | `#3949AB` |
| Model | `.mdc` | `#283593` |
| Model | `.md4` | `#1A237E` |
| Model | `.mdr` | `#4E342E` |
| Model | `.skb` | `#6D4C41` |
| Model | `.skd` | `#795548` |
| Model | `.mdm` | `#455A64` |
| Model | `.glm` | `#546E7A` |
| Model | `.iqm` | `#0277BD` |
| Model | `.md5mesh` | `#00838F` |
| Model | `.tan` | `#6D4C41` |
| Model | `.lwo` | `#7CB342` |
| Model | `.obj` | `#F57C00` |
| Asset | `.cfg` | `#455A64` |
| Asset | `.shader` | `#6A1B9A` |
| Asset | `.menu` | `#0277BD` |
| Asset | `.def` | `#00838F` |
| Asset | `.mtr` | `#7B1FA2` |
| Asset | `.txt` | `#546E7A` |
| Asset | `.json` | `#5C6BC0` |
| Asset | `.ttf` | `#00897B` |
| Asset | `.otf` | `#00ACC1` |
| Asset | `.spr` | `#D81B60` |
| Asset | `.sp2` | `#C2185B` |
| Asset | `.spr2` | `#AD1457` |
| Asset | `.bk` | `#AD1457` |
| Asset | `.os` | `#455A64` |
| Asset | `.dm2` | `#6D4C41` |
| Asset | `.aas` | `#00838F` |
| Asset | `.qvm` | `#3949AB` |
| Asset | `.crc` | `#546E7A` |
| Asset | `.tag` | `#795548` |
| Asset | `.mdx` | `#4E342E` |
| Asset | `.mds` | `#5D4037` |
| Asset | `.skc` | `#6D4C41` |
| Asset | `.ska` | `#8D6E63` |

## Notes

- In-app management is split into a dedicated **File -> File Associations...** dialog with tabbed **Archives**, **Images**, **Videos**, **Audio**, **Models**, and **Assets** lists.
- Windows registration writes per-extension `ProgID`/`DefaultIcon` entries plus `.ext\OpenWithProgids` and `Applications\pakfu.exe\SupportedTypes` under `HKCU\Software\Classes`.
- PakFu does not set the `.ext` default handler key; this keeps defaults user-managed while still listing PakFu in **Open with**.
- Icons are generated at registration time and cached in the app-local data directory (`file-association-icons`), with a fallback to the app icon if generation fails.
- Linux packages install `io.github.themuffinator.PakFu.desktop`, shared-mime-info globs, AppStream metadata, and a hicolor app icon so file managers can offer PakFu for supported formats.
- macOS packages declare the same managed extensions in `PakFu.app/Contents/Info.plist` document types, and the app handles Qt file-open events from Finder.
- `scripts/validate_shell_integration.py` keeps the Windows association list, Linux metadata, and macOS document type declarations in sync.
- `progs.dat` remains intentionally excluded from OS-level file associations because registering every `.dat` file would be too broad; PakFu still recognizes it when opened inside archives or folder views.
