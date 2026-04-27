# PakFu Support Matrix

This matrix is the user-facing support contract for current archive, preview,
and conversion behavior. It complements `docs/DEPENDENCIES.md`, which is the
implementation inventory.

Status labels:
- **Fixture-backed**: covered by the generated `support-matrix-fixtures` test.
- **Implementation-backed**: implemented in source and listed here, but not yet
  covered by a dedicated fixture or real-world corpus test.
- **Backend-dependent**: support depends on Qt Multimedia, platform codecs, or
  an external runtime component.
- **Read-only**: open/list/extract/preview behavior is supported, but save or
  rebuild is intentionally not exposed.

Run the matrix fixtures with:

```sh
meson test -C builddir support-matrix-fixtures
```

The fixtures are generated at test time from tiny synthetic payloads, so the
repository does not need copyrighted game assets to verify the core contract.

## Archive And Container Formats

| Type | Extensions | Open/List | Extract | Save/Rebuild | Evidence |
|---|---|---:|---:|---:|---|
| Folder input | directory path | Yes | Yes | N/A | Fixture-backed directory tree |
| PAK | `pak` | Yes | Yes | Yes | Fixture-backed `PACK` archive and `PakArchive::save_as` copy |
| SiN archive | `sin` | Yes | Yes | Yes | Fixture-backed `SPAK` archive and `PakArchive::save_as` copy |
| ZIP family | `zip`, `pk3`, `pk4`, `pkz` | Yes | Yes | Yes | Fixture-backed PK3 via `ZipArchive::write_rebuilt` |
| Quake Live Beta encrypted PK3 | `pk3` (XOR-obfuscated) | Yes | Yes | Yes | Fixture-backed encrypted PK3 encode/decode via `ZipArchive::write_rebuilt` |
| Doom 3 BFG resources | `resources` | Yes | Yes | No | Fixture-backed resources container; read-only backend |
| WAD2 | `wad`, `wad2` | Yes | Yes | Yes | Fixture-backed WAD2 via `WadArchive::write_wad2` |
| WAD3 | `wad3` | Yes | Yes | No | Fixture-backed WAD3 texture lump; read-only backend |
| Doom IWAD/PWAD | `wad` | Yes | Yes | No | Fixture-backed PWAD lump; read-only backend |

## Preview, Inspector, And Conversion Formats

| Area | Formats | Current status | Evidence |
|---|---|---|---|
| Core image preview | `pcx`, `wal`, `png`, `tga`, `jpg`, `jpeg` | Supported | TGA decode and PCX encode/decode are fixture-backed; PNG/JPEG use Qt; WAL requires a 256-color Quake II palette |
| Additional image preview | `bmp`, `gif`, `tif`, `tiff`, `swl`, `m8`, `mip`, `lmp`, `dds`, `ftx` | Supported | M8 is fixture-backed; other loaders are implementation-backed built-ins or Qt image readers; M8/SWL use embedded palettes and expose mip previews |
| Image conversion output | `png`, `jpg`, `bmp`, `gif`, `tga`, `tiff`, `pcx`, `wal`, `swl`, `mip`, `lmp`, `ftx`, `dds` | Supported | PCX output is fixture-backed; remaining formats are implementation-backed and format-aware |
| Audio playback | `wav`, `ogg`, `mp3`, `idwav` | Supported | Backend-dependent playback; IDWAV is converted to WAV for supported embedded codecs |
| Built-in cinematics | `cin`, `roq` | Supported | Implementation-backed built-in decoders |
| Backend video playback | `bik`, `ogv`, `mp4`, `mkv`, `avi`, `webm` | Supported when codecs are available | Backend-dependent Qt Multimedia playback |
| Models | `mdl`, `md2`, `fm`, `md3`, `mdc`, `md4`, `mdr`, `skb`, `skd`, `mdm`, `glm`, `iqm`, `md5mesh`, `tan`, `obj`, `lwo` | Supported | FM is fixture-backed; other loaders are implementation-backed; FM resolves Heretic II `.m8` skins |
| BSP/map preview | `bsp` | Supported | Heretic II native `IBSP` and converted `QBSP` BSPs are fixture-backed; Quake/Quake II/Quake III-family loaders are implementation-backed |
| idTech4 map inspection | `map`, `proc` | Text/metadata inspection supported; no 3D idTech4 renderer is claimed | Fixture-backed synthetic `.map` source and `.proc` render-description summaries |
| idTech sprite/assets | `spr`, `sp2`, `spr2`, `bk`, `os` | Supported | BK/OS are fixture-backed; SPR/SP2 are implementation-backed animated previews and metadata views |
| idTech inspectors | `dm2`, `aas`, `qvm`, `progs.dat`, `tag`, `mdx`, `mds`, `skc`, `ska`, `ttf`, `otf` | Supported | Implementation-backed metadata/insight views |
| Text and script assets | `cfg`, `config`, `rc`, `arena`, `bot`, `skin`, `shader`, `menu`, `def`, `mtr`, `map`, `proc`, `ent`, `qc`, and similar plain text | Supported | Implementation-backed text preview with syntax highlighting for common script/config families; `.map`/`.proc` add map-scope summaries |

## Game Installation Profiles

| Area | Current status | Evidence |
|---|---|---|
| Heretic II gamepack auto-detection | Supported | Fixture-backed by `game-auto-detect`; recognizes full-install `base/htic2-*.pak`, root-level `htic2-*.pak`, `base/book`, `base/ds`, and common executable/folder aliases |

## Maintenance Rules

When a row changes, update this document, the README summary, and the
`support-matrix-fixtures` test when synthetic fixtures can prove the claim.
Formats that require copyrighted game samples or large real-world corpora should
remain marked as implementation-backed until a redistributable corpus exists.
