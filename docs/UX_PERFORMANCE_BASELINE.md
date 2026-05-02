# UX Performance Baseline

This file records the current roadmap performance evidence. It uses generated
fixtures only, so no private game assets, archive paths, file names, or user
content are stored.

## 2026-05-01 Local Baseline

Environment:

- Platform: Windows development workstation
- Build: `builddir/src/pakfu.exe` after `meson compile -C builddir`
- Corpus: generated folder fixture with 1,000 text files across 20 directories,
  each file containing a short ASCII payload
- Timing method: PowerShell `System.Diagnostics.Stopwatch`

| Scenario | Command shape | Result |
|---|---|---:|
| Folder summary | `pakfu.exe --cli --info <fixture-folder>` | 42.5 ms |
| Folder listing | `pakfu.exe --cli --list <fixture-folder>` | 24.8 ms |
| Folder validation | `pakfu.exe --cli --validate <fixture-folder>` | 27.8 ms |
| Folder package manifest | `pakfu.exe --cli --package-manifest json <fixture-folder>` | 26.2 ms |

Acceptance:

- All measured commands returned without CLI errors.
- The fixture size is intentionally modest and generated; release QA should add
  larger private corpus runs outside the repository.
- Performance traces are privacy-safe: opt-in timing details use item kind,
  extension/archive kind, size buckets, and step labels rather than paths,
  file names, search queries, or asset content.

## Reproduction Sketch

Create a temporary fixture outside the repository, run the CLI operations above,
and delete the fixture after timings are captured. Release baselines may include
larger private corpora, but the stored report should describe them by size and
format mix rather than by user or file names.
