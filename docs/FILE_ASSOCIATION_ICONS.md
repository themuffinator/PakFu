# File Association Icons

PakFu uses a per-extension icon set for managed archive, image, video, audio, and model associations so each file type is visually distinct and readable in file managers.

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
| Image | `.pcx` | `#546E7A` |
| Image | `.wal` | `#1565C0` |
| Image | `.swl` | `#2E7D32` |
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
| Model | `.lwo` | `#7CB342` |
| Model | `.obj` | `#F57C00` |

## Notes

- In-app management is split into a dedicated **File -> File Associations...** dialog with tabbed **Archives**, **Images**, **Videos**, **Audio**, and **Models** lists.
- Windows registration writes per-extension `ProgID` entries and `DefaultIcon` paths under `HKCU\Software\Classes`.
- Icons are generated at registration time and cached in the app-local data directory (`file-association-icons`), with a fallback to the app icon if generation fails.
- macOS and Linux associations are installer-managed in this repository; the dialog reports this state and keeps controls read-only. The extension list above is the canonical target set for installer integration.
