#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import sys
from pathlib import Path


def strip_entry_heading(entry: str) -> str:
    lines = entry.splitlines()
    if lines and lines[0].startswith("## ["):
        return "\n".join(lines[1:]).strip()
    return entry.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract release notes from CHANGELOG.md.")
    parser.add_argument("--version", required=True, help="Version or tag (e.g. v1.2.3).")
    parser.add_argument("--output", help="Write notes to this file.")
    args = parser.parse_args()

    version = args.version.lstrip("vV")
    changelog = Path(__file__).resolve().parent.parent / "CHANGELOG.md"
    if not changelog.exists():
        print("CHANGELOG.md not found.", file=sys.stderr)
        return 1

    lines = changelog.read_text(encoding="utf-8").splitlines()
    start = None
    for idx, line in enumerate(lines):
        if line.startswith(f"## [{version}]"):
            start = idx
            break
    if start is None:
        print(f"Version {version} not found in CHANGELOG.md.", file=sys.stderr)
        return 1

    end = len(lines)
    for idx in range(start + 1, len(lines)):
        if lines[idx].startswith("## ["):
            end = idx
            break

    entry = "\n".join(lines[start:end]).rstrip()
    generated = dt.datetime.now(dt.UTC).strftime("%Y-%m-%d %H:%M UTC")
    notes = "\n".join(
        [
            f"# PakFu v{version}",
            "",
            f"Generated: {generated}",
            "",
            "## What's New",
            "",
            strip_entry_heading(entry),
            "",
            "Technical build and repository-only changes are omitted unless they affect downloads, installs, or everyday use.",
        ]
    ).rstrip() + "\n"

    if args.output:
        Path(args.output).write_text(notes, encoding="utf-8")
    else:
        print(notes)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
