# Roadmap Completion And Release Hardening

This document is the acceptance ledger for the UX roadmap work in
`docs/proposals/ux-workflow-analysis-1-may-2026.md`. It complements
`docs/UX_VALIDATION.md` and the static audit script
`scripts/ux_roadmap_audit.py`.

The roadmap implementation is complete when the code, docs, and local
verification evidence in this file are present. Release QA should still rerun
the manual/runtime gates before any tag that advertises the UX roadmap, because
platform, scale, and fixture evidence must be refreshed per release build.

## Roadmap Evidence Ledger

| Roadmap item | Completion signal | Static audit support | Runtime/release artifact |
|---|---|---|---|
| Keyboard focus styling and operability | Focus styling exists and keyboard matrix is defined | Search QSS for `:focus`, accessible names, shortcut/search controls | `--qa-practical` plus the keyboard matrix in `docs/UX_VALIDATION.md` |
| Pointer target hardening | Toolbars and icon buttons have target-size rules | Search QSS for minimum sizing and button padding | Target-size review sheet in release QA |
| Contrast hardening | Theme token contrast matrix is documented | Theme/style inventory and token docs | `docs/THEME_CONTRAST_MATRIX.md` |
| Layout/scale hardening | Scale audit states and acceptance criteria are documented | Static scan can flag fixed sizes and dense surfaces | Layout screenshot set in release QA |
| Status/modality cleanup | Modal inventory is classified and noncritical information dialogs are removed | Static modal inventory from `QMessageBox`, `QDialog`, and `exec()` usage | Modal decision template below |
| i18n foundation | Translator bootstrap plus app-shell translation coverage exist | Search `QTranslator`, `installTranslator`, `tr()`, `QCoreApplication::translate()` | Pseudo-localization run in release QA |
| Model/view migration | Large browsers use model/view where scale matters | Search `QAbstractItemModel`, `QTableView`, `QTreeView`, residual item widgets | Practical archive QA and large-archive release benchmark |
| Performance baseline | Representative operations have timing/profile signals | Search `QElapsedTimer`, performance helpers, preview profiles | `docs/UX_PERFORMANCE_BASELINE.md` |
| Consistency pass | Verbs, icons, statuses, and destructive patterns are reviewed | Docs and static action inventories | Consistency review sheet in release QA |
| Telemetry plan | Event inventory and privacy rules exist before instrumentation | Search telemetry/analytics/logging terms | Privacy-reviewed event list in `docs/UX_VALIDATION.md` |
| A/B validation | High-friction changes have variants and metrics | Docs presence check | A/B validation plan in `docs/UX_VALIDATION.md` |
| Release hardening | Completion checklist is evidence-backed and free of placeholder statuses | Script report plus release docs | This ledger plus `scripts/validate_build.py` |

## Static Audit Command

Run this from the repository root:

```sh
python scripts/ux_roadmap_audit.py --strict
```

Optional machine-readable output:

```sh
python scripts/ux_roadmap_audit.py --strict --format json
```

Strict mode is the roadmap completion gate. It fails required checks, warning
checks, and placeholder completion-ledger statuses.

## Current Completion Checklist

This table records the implementation evidence for the current roadmap sweep.
Release-gated rows are complete as roadmap artifacts because the acceptance
matrix and evidence templates exist; the concrete platform captures should be
refreshed during each tagged release process.

| Area | Evidence location | Status |
|---|---|---|
| Static roadmap audit | `python scripts/ux_roadmap_audit.py --strict` | Complete |
| Keyboard QA | `docs/UX_VALIDATION.md` keyboard matrix; `--qa-practical` smoke coverage | Complete |
| Pointer/target QA | `docs/UX_VALIDATION.md` pointer matrix; theme target-size rules | Complete |
| Contrast | `docs/THEME_CONTRAST_MATRIX.md` | Complete |
| Layout/scale | `docs/UX_VALIDATION.md` layout and scale audit matrix | Complete |
| Modality | Static modal inventory, no `QMessageBox::information`, decision template below | Complete |
| i18n | Translator bootstrap in `src/main.cpp`; shell/dialog `tr()` coverage | Complete |
| Performance | `foundation/performance_metrics` signals; `docs/UX_PERFORMANCE_BASELINE.md` | Complete |
| Consistency | `docs/UX_VALIDATION.md` consistency review sheet; `docs/UI_BUTTON_ICONS.md` | Complete |
| Telemetry | Privacy-safe event inventory in `docs/UX_VALIDATION.md`; redacted UX trace payloads | Complete |
| A/B validation | `docs/UX_VALIDATION.md` A/B validation plan | Complete |
| Docs | README links the UX validation docs; changed workflows are documented | Complete |
| Build validation | `meson compile -C builddir`; `meson test -C builddir --print-errorlogs` | Complete |
| Practical QA | `.\builddir\src\pakfu.exe --cli --qa-practical` | Complete |
| Release assets | `scripts/validate_build.py`; `scripts/validate_release_assets.py`; `docs/RELEASES.md` | Complete |

## Release Revalidation Commands

Use the commands below when preparing a release that includes these roadmap
changes. Replace the binary path and version as appropriate for the platform.

```sh
python scripts/ux_roadmap_audit.py --strict
python scripts/sync_doc_versions.py --check
meson compile -C builddir
meson test -C builddir --print-errorlogs
python scripts/validate_build.py --binary builddir/src/pakfu --expected-version "$(cat VERSION)" --run-practical-qa
python scripts/validate_release_assets.py --version "$(cat VERSION)" --dist dist
```

On Windows, the binary path is usually `builddir\src\pakfu.exe`.

## Manual Review Templates

### Keyboard QA Result

```text
Platform:
Qt version:
PakFu version/commit:
Scale and font settings:
Archive/profile fixture:
Workflow:
Passed:
Findings:
Blockers:
Follow-up issue:
```

### Contrast Result

```text
Theme:
Token/state:
Foreground:
Background:
Measured ratio:
Pass/fail:
Screenshot/reference:
Follow-up issue:
```

### Benchmark Result

```text
Platform:
PakFu version/commit:
Corpus description:
Operation:
Cold result:
Warm result:
Memory high-water mark:
Regression versus baseline:
Notes:
```

### Modal Decision

```text
Source/surface:
Modal purpose:
Current trigger:
Decision: keep / replace / merge / defer
Reason:
Target milestone:
Follow-up issue:
```

## Definition Of Done For Roadmap Claims

A roadmap item is complete when:

- The code or docs change that implements the behavior is present.
- The relevant static signal is present or an explicit exception is documented.
- The runtime/manual artifact listed in the evidence ledger has a template or
  current result.
- User-facing docs and CLI help are updated if behavior changed.
- Release notes mention any workflow, accessibility, i18n, or compatibility
  impact when the changes are tagged.

Do not mark a UX roadmap item complete based only on implementation code. The
roadmap is complete when the behavior is implemented, measured where possible,
documented, and release-reviewable.
