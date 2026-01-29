# PakFu Releases

## Versioning
PakFu follows SemVer. Update `VERSION` for every release and tag the commit with
`vX.Y.Z`.

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
2. Commit changes and create a tag: `git tag vX.Y.Z`.
3. Push commits and tag: `git push && git push --tags`.
4. GitHub Actions builds and uploads platform packages automatically.

The release workflow uses the repository name as the update source and publishes
the artifacts to the GitHub Release matching the tag.
