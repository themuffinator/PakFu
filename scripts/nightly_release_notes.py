#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import sys
from pathlib import Path


def extract_changelog_entry(lines: list[str], version: str) -> str:
    start = None
    for idx, line in enumerate(lines):
        if line.startswith(f"## [{version}]"):
            start = idx
            break
    if start is None:
        raise ValueError(f"Version {version} not found in changelog.")

    end = len(lines)
    for idx in range(start + 1, len(lines)):
        if lines[idx].startswith("## ["):
            end = idx
            break
    return "\n".join(lines[start:end]).rstrip()


def main() -> int:
    parser = argparse.ArgumentParser(description="Create nightly release notes from CHANGELOG.md.")
    parser.add_argument("--version", required=True, help="Version or tag (e.g. v1.2.3.4).")
    parser.add_argument("--changelog", default="CHANGELOG.md", help="Path to changelog file.")
    parser.add_argument("--output", required=True, help="Output markdown file.")
    parser.add_argument(
        "--commit-range",
        help="Optional commit range text shown in the notes (e.g. v1.2.3.3..HEAD).",
    )
    parser.add_argument(
        "--include-full-changelog",
        action="store_true",
        help="Append the full changelog content to the notes.",
    )
    args = parser.parse_args()

    version = args.version.lstrip("vV")
    changelog_path = Path(args.changelog).resolve()
    if not changelog_path.exists():
        print(f"Changelog file not found: {changelog_path}", file=sys.stderr)
        return 1

    lines = changelog_path.read_text(encoding="utf-8").splitlines()
    try:
        entry = extract_changelog_entry(lines, version)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    utc_now = dt.datetime.now(dt.UTC).strftime("%Y-%m-%d %H:%M UTC")
    notes = [
        f"# PakFu Nightly v{version}",
        "",
        f"Generated: {utc_now}",
    ]
    if args.commit_range:
        notes.append(f"Commit range: `{args.commit_range}`")
    notes.extend(
        [
            "",
            "## Nightly Changes",
            "",
            entry,
        ]
    )

    if args.include_full_changelog:
        notes.extend(
            [
                "",
                "## Full Changelog",
                "",
                "\n".join(lines).rstrip(),
            ]
        )

    out_path = Path(args.output).resolve()
    out_path.write_text("\n".join(notes).rstrip() + "\n", encoding="utf-8")
    print(f"Wrote release notes: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
