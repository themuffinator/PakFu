# PakFu Releases

## Versioning
PakFu follows SemVer with an updater-friendly, numeric-only identifier.

Rules:
- `VERSION` is the source of truth and must be strictly numeric dot segments
  (no leading `v`, no `-beta`/`-rc`, no `+git` metadata).
- Git tags must be `v<version>` and match `VERSION` exactly.
- Stable releases use `MAJOR.MINOR.PATCH` (example: `1.4.0`).
- Beta/dev releases use `MAJOR.MINOR.PATCH.BUILD` (example: `1.5.0.3`), and the
  GitHub Release must be marked as **prerelease**.
- Always increment the numeric portion for every published build; never reuse
  a version number.

Why: the auto-updater compares versions using Qt's numeric `QVersionNumber`
parsing, which ignores suffixes like `-beta`. Numeric-only identifiers keep
update ordering reliable.

Examples:
- Stable: `1.2.0`
- Beta/dev for the next stable: `1.3.0.1`, `1.3.0.2`

## Release Assets
To enable in-app updates, attach platform packages to each GitHub Release.
Current packaging targets:
- Windows: portable `.zip` (installer `.exe/.msi` planned)
- macOS: portable `.zip` (installer `.dmg/.pkg` planned)
- Linux: `.tar.gz` (AppImage planned)

Asset names should include platform hints (e.g. `win`, `mac`, `linux`, `x64`) so
the updater can select the correct file automatically.

## Release Workflow
1. Update `VERSION`.
2. Commit changes and create a tag: `git tag vX.Y.Z` (or `vX.Y.Z.BUILD`).
3. Push commits and tag: `git push && git push --tags`.
4. GitHub Actions builds and uploads platform packages automatically.
5. For beta/dev builds, ensure the GitHub Release is marked **prerelease** so
   non-stable update channels can see it.

The release workflow uses the repository name as the update source and publishes
the artifacts to the GitHub Release matching the tag.
