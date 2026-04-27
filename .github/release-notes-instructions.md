# PakFu Release Notes Instructions

Write release notes for people downloading and using PakFu, not for people
reviewing commits.

## Voice
- Use a warm, practical, user-friendly tone.
- Be concise and specific.
- Prefer present tense.
- Avoid internal implementation language unless it directly helps a user decide
  whether to download the release.

## Structure
- Start with `## What's New`.
- Use short sections only when they help scanning:
  - `### Highlights`
  - `### Preview and Format Support`
  - `### Reliability and Polish`
  - `### Installers and Updates`
- Omit empty sections.
- Keep the full note to roughly 3-7 bullets for normal nightlies.

## What To Include
- User-visible features, format support, previews, CLI workflows, archive
  behavior, installers, update behavior, reliability, startup responsiveness,
  performance, and memory improvements.
- Group related implementation steps into one user-facing benefit.
- If several commits work toward the same goal, write one bullet for the goal.

## What To Skip
- CI-only changes, test-only changes, repository housekeeping, release plumbing,
  changelog maintenance, dependency/build-system churn, and refactors with no
  clear user benefit.
- Do not paste or summarize the entire historical changelog.
- Do not include raw commit lists, author credits, or PR numbers unless a release
  manager explicitly asks for them.

## Wording Rules
- Say "improves general performance" or "reduces memory use" when the technical
  detail is not useful to a normal user.
- Say "packages are more reliable" instead of listing every packaging script or
  workflow change.
- Avoid repeating the same feature under multiple bullets.
