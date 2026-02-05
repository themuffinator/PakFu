#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import re
import subprocess
from pathlib import Path

TYPE_TITLES = {
    "feat": "Added",
    "fix": "Fixed",
    "perf": "Performance",
    "refactor": "Changed",
    "docs": "Documentation",
    "build": "Build",
    "ci": "CI",
    "test": "Tests",
    "style": "Style",
    "chore": "Chore",
}

SECTION_ORDER = [
    "Breaking Changes",
    "Added",
    "Fixed",
    "Changed",
    "Performance",
    "Documentation",
    "Build",
    "CI",
    "Tests",
    "Style",
    "Chore",
    "Other",
]

SUBJECT_RE = re.compile(r"^(?P<type>[a-zA-Z]+)(\(.+\))?(?P<bang>!)?:\s*(?P<desc>.+)")


def run_git(args: list[str], cwd: Path) -> str:
    return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()


def last_tag(repo: Path) -> str | None:
    try:
        return run_git(["describe", "--tags", "--abbrev=0"], repo)
    except subprocess.CalledProcessError:
        return None


def commit_log(repo: Path, base_tag: str | None) -> list[tuple[str, str]]:
    if base_tag:
        range_spec = f"{base_tag}..HEAD"
    else:
        range_spec = "HEAD"
    log = run_git(["log", range_spec, "--pretty=format:%s%n%b%n==END=="], repo)
    entries = [entry.strip() for entry in log.split("==END==") if entry.strip()]
    commits = []
    for entry in entries:
        lines = entry.splitlines()
        subject = lines[0].strip() if lines else ""
        body = "\n".join(lines[1:]) if len(lines) > 1 else ""
        commits.append((subject, body))
    return commits


def normalize_subject(subject: str) -> tuple[str | None, str, bool]:
    if subject.startswith("Merge "):
        return None, "", False
    if subject.startswith("chore(release):"):
        return None, "", False
    match = SUBJECT_RE.match(subject)
    breaking = False
    if match:
        change_type = match.group("type").lower()
        desc = match.group("desc").strip()
        breaking = match.group("bang") == "!"
        return change_type, desc, breaking
    return None, subject.strip(), False


def build_sections(commits: list[tuple[str, str]]) -> dict[str, list[str]]:
    sections: dict[str, list[str]] = {}
    for subject, body in commits:
        change_type, desc, breaking = normalize_subject(subject)
        if not desc:
            continue
        if "BREAKING CHANGE" in body:
            breaking = True
        if breaking:
            sections.setdefault("Breaking Changes", []).append(desc)
            continue
        title = TYPE_TITLES.get(change_type, "Other")
        sections.setdefault(title, []).append(desc)
    return sections


def load_changelog(path: Path) -> list[str]:
    if not path.exists():
        return [
            "# Changelog",
            "",
            "All notable changes to PakFu are documented here.",
            "",
        ]
    return path.read_text(encoding="utf-8").splitlines()


def write_changelog(path: Path, lines: list[str]) -> None:
    text = "\n".join(lines).rstrip() + "\n"
    path.write_text(text, encoding="utf-8")


def insert_entry(lines: list[str], entry: list[str]) -> list[str]:
    for line in lines:
        if line.startswith(entry[0]):
            return lines
    insert_at = len(lines)
    for idx, line in enumerate(lines):
        if line.startswith("## ["):
            insert_at = idx
            break
    return lines[:insert_at] + entry + [""] + lines[insert_at:]


def main() -> int:
    parser = argparse.ArgumentParser(description="Update CHANGELOG.md from git history.")
    parser.add_argument(
        "--version",
        help="Version to add (defaults to VERSION file).",
    )
    parser.add_argument(
        "--date",
        help="Release date (YYYY-MM-DD). Defaults to UTC today.",
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    version_file = repo / "VERSION"
    version = args.version or version_file.read_text(encoding="utf-8").strip()
    version = version.lstrip("vV")
    date = args.date or dt.datetime.utcnow().date().isoformat()

    base_tag = last_tag(repo)
    commits = commit_log(repo, base_tag)
    sections = build_sections(commits)

    entry = [f"## [{version}] - {date}"]
    if not sections:
        entry += ["### Changed", "- No user-facing changes."]
    else:
        for title in SECTION_ORDER:
            items = sections.get(title, [])
            if not items:
                continue
            entry.append(f"### {title}")
            for item in items:
                entry.append(f"- {item}")

    changelog_path = repo / "CHANGELOG.md"
    lines = load_changelog(changelog_path)
    updated = insert_entry(lines, entry)
    write_changelog(changelog_path, updated)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
