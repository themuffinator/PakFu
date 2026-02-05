# PakFu Releases

## Versioning
PakFu follows SemVer with an updater-friendly, numeric-only identifier. Versions
are computed automatically from git history by GitHub Actions.

Rules:
- `VERSION` is written by automation and must stay strictly numeric dot segments
  (no leading `v`, no `-beta`/`-rc`, no `+git` metadata).
- Git tags are `v<version>` and must match `VERSION` exactly.
- Stable releases use `MAJOR.MINOR.PATCH` (example: `1.4.0`).
- Beta/dev releases use `MAJOR.MINOR.PATCH.BUILD` (example: `1.5.0.3`), and the
  GitHub Release is marked **prerelease** automatically.
- Always increment the numeric portion for every published build; never reuse
  a version number.

Why: the auto-updater compares versions using Qt's numeric `QVersionNumber`
parsing, which ignores suffixes like `-beta`. Numeric-only identifiers keep
update ordering reliable.

Automatic bump rules (based on Conventional Commit style since the last stable
tag):
- Breaking change (`type!:` in the subject or `BREAKING CHANGE` in the body)
  bumps **MAJOR** (or **MINOR** if `MAJOR=0`).
- `feat:` bumps **MINOR**.
- Everything else bumps **PATCH**.

Stage/maturity rule:
- While `MAJOR=0` (pre-1.0), breaking changes advance **MINOR** instead of
  **MAJOR** to reflect ongoing development.

Examples:
- Stable: `1.2.0`
- Beta/dev for the next stable: `1.3.0.1`, `1.3.0.2`

Preview the next version locally:
```sh
python scripts/next_version.py --channel dev
```

## Release Assets
To enable in-app updates, attach platform packages to each GitHub Release.
Current packaging targets:
- Windows: portable `.zip` (installer `.exe/.msi` planned)
- macOS: portable `.zip` (installer `.dmg/.pkg` planned)
- Linux: `.tar.gz` (AppImage planned)

Asset names should include platform hints (e.g. `win`, `mac`, `linux`, `x64`) so
the updater can select the correct file automatically.

## Release Workflow
1. Push to `main` (default: auto-creates a **dev** prerelease).
2. The `auto-version` workflow computes the next version, updates `VERSION`,
   commits, and tags it.
3. The `release` workflow builds and publishes packages for the tag.
4. For a stable release, run the `auto-version` workflow manually with
   `channel=stable`.

The release workflow uses the repository name as the update source and publishes
the artifacts to the GitHub Release matching the tag.
