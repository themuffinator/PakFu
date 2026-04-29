# PakFu Releases

## Versioning Policy
PakFu uses numeric-only version identifiers so Qt `QVersionNumber` comparisons
stay stable for updater decisions.

Rules:
- `VERSION` must always be numeric dot-segments only (no `v`, no suffixes).
- Git tags are always `v<version>`.
- Stable releases use `MAJOR.MINOR.PATCH`.
- Nightly releases use `MAJOR.MINOR.PATCH.BUILD`.
- `scripts/sync_doc_versions.py` keeps the README version badge and current
  documentation version declarations aligned with `VERSION`.

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
- Release notes are user-facing summaries generated from the current release's
  changelog entry, not full historical changelog dumps.
- Technical-only changes are omitted unless they affect downloads, installs,
  startup behavior, performance, memory use, format support, previews, archive
  behavior, or CLI workflows.
- Related commits should be grouped into one user benefit instead of listed as
  separate implementation steps.
- New game support and filetype support must be kept in the summary even when
  the implementation spans loaders, viewers, file associations, fixtures, and
  documentation. Collapse the work into a format-focused user benefit instead of
  dropping it as internal plumbing.
- Vague roll-up commits are summarized from their touched product areas as well
  as their subject line, so broad implementation commits can still produce a few
  useful collapsed bullets.
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
Runtime update checks are asynchronous and do not block the main window from
opening. When the GUI updater downloads an asset, it first downloads the
matching `pakfu-<version>-release-manifest.json` asset and verifies the selected
package by exact asset name, byte size, and SHA-256 before opening the downloaded
folder or launching an installer handoff.

Linux portable archives are produced from the same deployed AppDir used for the
AppImage, so Qt and other runtime libraries are bundled in both Linux assets.
The archive includes a top-level `pakfu` launcher for extracted-directory use.
The AppDir also carries the PakFu desktop entry, shared-mime-info package,
AppStream metadata, and hicolor icon so AppImage/desktop integration can expose
the same supported file types as the app.

macOS bundles declare PakFu document types in `Contents/Info.plist`, and Windows
builds keep file associations user-managed through the in-app Open With
registration UI.

The release pipeline also emits:
- `pakfu-<version>-release-manifest.json` (checksums + distribution metadata)

Standalone raw binaries (`pakfu`, `pakfu.exe`) and standalone changelog assets
(`CHANGELOG-<version>.md`) are not release assets. `scripts/validate_release_assets.py`
fails the release if unexpected files are present in `dist`.

Each installer and portable package includes a packaged HTML user guide generated
from the user-facing sections of `README.md` by `scripts/build_user_guide.py`.
The guide is stored where users expect product documentation:
- Windows portable/MSI payload: `Documentation/index.html`
- macOS portable archive: `Documentation/index.html` beside `PakFu.app`
- macOS installer app bundle: `PakFu.app/Contents/Resources/Documentation/index.html`
- Linux portable archive: `Documentation/index.html` at the archive root, plus
  `/usr/share/doc/pakfu/index.html` in the bundled runtime tree
- Linux AppImage: `/usr/share/doc/pakfu/index.html` inside the AppDir

Validation tooling:
- `scripts/sync_doc_versions.py`
- `scripts/validate_build.py`
- `scripts/validate_release_assets.py`
- `scripts/release_manifest.py`
- `scripts/build_user_guide.py`

`scripts/validate_build.py` treats the CLI as a platform contract. In addition
to version/help checks, it runs `--platform-report` and `--plugin-report` so
packaged binaries prove their runtime paths, extension discovery, and
integration diagnostics are available on every supported OS.

## Workflows
Pull request gates:
- Workflow: `.github/workflows/pr-ci.yml`
- Trigger: pull requests, pushes to `main`, and manual dispatch
- Required checks to configure in branch protection:
1. `build-test-windows-latest`
2. `build-test-macos-latest`
3. `build-test-ubuntu-latest`
4. `sanitize-and-fuzz` from `.github/workflows/hardening.yml`
- Coverage: each platform configures a non-packaging Meson build, compiles the app and core tests, runs the support-matrix fixture explicitly, runs the full Meson test suite, and validates the CLI binary including platform and extension diagnostics. The hardening gate runs the sanitizer/libFuzzer smoke build where Clang support is available.

Nightly automation:
- Workflow: `.github/workflows/nightly.yml`
- Trigger: scheduled nightly + manual dispatch (`force` optional)
- Stages:
1. `prepare`: compute nightly version + change gate
2. `build`: compile on Windows/macOS/Linux
3. `validate`: run CLI smoke checks plus `--platform-report`/`--plugin-report` on each platform build (and optionally `--run-practical-qa` for UI file-ops smoke checks)
4. `package`: build installer + portable assets per platform
5. `release`: tag, validate completeness, publish nightly release with curated user-facing notes

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

Check documentation version declarations:
```sh
python scripts/sync_doc_versions.py --check
```

Preview the packaged HTML user guide:
```sh
python scripts/build_user_guide.py --output dist/Documentation
```
