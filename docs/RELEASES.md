# PakFu Releases

## Versioning Policy
PakFu uses numeric-only version identifiers so Qt `QVersionNumber` comparisons
stay stable for updater decisions.

Rules:
- `VERSION` must always be numeric dot-segments only (no `v`, no suffixes).
- Git tags are always `v<version>`.
- Stable releases use `MAJOR.MINOR.PATCH`.
- Nightly releases use `MAJOR.MINOR.PATCH.BUILD`.

Nightly version generation:
- Computed by `scripts/nightly_version.py`.
- Base (`MAJOR.MINOR.PATCH`) is derived from Conventional Commit bump rules
  relative to the latest stable tag.
- `BUILD` increments for each nightly on that base.
- Nightly runs only publish when meaningful commits exist since the previous
  nightly tag (`chore(release): ...` commits are ignored).

Stable/beta/dev manual version generation:
- Computed by `scripts/next_version.py` for `auto_version.yml`.

## Changelog Policy
- `CHANGELOG.md` remains the source of truth.
- Nightly release notes include:
1. The generated nightly changelog entry.
2. The full `CHANGELOG.md` content.
- Changelog/release-note rendering scripts:
1. `scripts/update_changelog.py`
2. `scripts/nightly_release_notes.py`
3. `scripts/release_notes.py` (manual channel releases)

## Distribution Contract
Packaged artifacts must follow this canonical pattern:
`pakfu-<version>-<platform>-<arch>-<kind>.<ext>`

Required assets per platform:
- Windows: `portable.zip` and `installer.msi`
- macOS: `portable.zip` and `installer.pkg`
- Linux: `portable.tar.gz` and `installer.AppImage`

Current updater behavior prefers installer assets and falls back to portable
archives when needed.

The release pipeline also emits:
- `pakfu-<version>-release-manifest.json` (checksums + distribution metadata)
- `CHANGELOG-<version>.md`

Validation tooling:
- `scripts/validate_build.py`
- `scripts/validate_release_assets.py`
- `scripts/release_manifest.py`

## Workflows
Nightly automation:
- Workflow: `.github/workflows/nightly.yml`
- Trigger: scheduled nightly + manual dispatch (`force` optional)
- Stages:
1. `prepare`: compute nightly version + change gate
2. `build`: compile on Windows/macOS/Linux
3. `validate`: run CLI smoke checks on each platform build
4. `package`: build installer + portable assets per platform
5. `release`: tag, validate completeness, publish prerelease with full changelog context

Manual release channels:
- Workflow: `.github/workflows/auto_version.yml`
- Trigger: manual dispatch only
- Purpose: stable/beta/dev tagged releases from selected channel policy

Manual rebuild of an existing ref:
- Workflow: `.github/workflows/release.yml`

## Local Preview Commands
Preview manual next version:
```sh
python scripts/next_version.py --channel dev
```

Preview nightly decision/version:
```sh
python scripts/nightly_version.py --format json
```
