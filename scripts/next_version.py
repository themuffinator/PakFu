#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

VERSION_RE = re.compile(r"^v?(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:\.(\d+))?$")
FEATURE_RE = re.compile(r"^feat(\(.+\))?:", re.IGNORECASE)
BREAKING_RE = re.compile(r"^\w+(\(.+\))?!:")


def run_git(args: list[str], cwd: Path) -> str:
    try:
        return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()
    except subprocess.CalledProcessError as exc:
        msg = exc.output if exc.output else str(exc)
        raise RuntimeError(msg) from exc


def parse_version(text: str) -> list[int] | None:
    match = VERSION_RE.match(text.strip())
    if not match:
        return None
    parts = [int(group) for group in match.groups() if group is not None]
    if len(parts) < 3:
        return None
    return parts


def format_version(parts: list[int]) -> str:
    return ".".join(str(p) for p in parts)


def read_version_file(path: Path) -> list[int]:
    raw = path.read_text(encoding="utf-8").strip()
    parts = parse_version(raw)
    if not parts:
        raise RuntimeError(f"VERSION is not numeric: {raw!r}")
    return parts[:3]


def list_tags(repo: Path) -> list[tuple[str, list[int]]]:
    tags_raw = run_git(["tag", "--list", "v*"], cwd=repo)
    tags = []
    for tag in tags_raw.splitlines():
        parts = parse_version(tag)
        if parts:
            tags.append((tag, parts))
    return tags


def latest_stable_tag(tags: list[tuple[str, list[int]]]) -> tuple[str, list[int]] | None:
    stable = [(tag, parts) for tag, parts in tags if len(parts) == 3]
    if not stable:
        return None
    return max(stable, key=lambda item: tuple(item[1]))


def commits_since(repo: Path, base_tag: str | None) -> list[tuple[str, str]]:
    if base_tag:
        range_spec = f"{base_tag}..HEAD"
    else:
        range_spec = "HEAD"
    log = run_git(["log", range_spec, "--pretty=format:%s%n%b%n==END=="], cwd=repo)
    entries = [entry.strip() for entry in log.split("==END==") if entry.strip()]
    commits = []
    for entry in entries:
        lines = entry.splitlines()
        subject = lines[0] if lines else ""
        body = "\n".join(lines[1:]) if len(lines) > 1 else ""
        commits.append((subject.strip(), body))
    return commits


def classify_bump(commits: list[tuple[str, str]]) -> str | None:
    if not commits:
        return None
    bump = "patch"
    for subject, body in commits:
        if BREAKING_RE.match(subject) or "BREAKING CHANGE" in body:
            return "major"
        if FEATURE_RE.match(subject):
            bump = "minor"
    return bump


def bump_version(base: list[int], bump: str) -> list[int]:
    major, minor, patch = base
    if bump == "major":
        if major == 0:
            minor += 1
            patch = 0
        else:
            major += 1
            minor = 0
            patch = 0
    elif bump == "minor":
        minor += 1
        patch = 0
    else:
        patch += 1
    return [major, minor, patch]


def next_build_for_base(tags: list[tuple[str, list[int]]], base: list[int]) -> int:
    max_build = 0
    for _, parts in tags:
        if len(parts) != 4:
            continue
        if parts[:3] == base:
            max_build = max(max_build, parts[3])
    return max_build + 1


def compute_next_version(repo: Path, channel: str, allow_empty: bool) -> list[int]:
    tags = list_tags(repo)
    stable_tag = latest_stable_tag(tags)
    if stable_tag:
        base_tag, base_version = stable_tag
    else:
        base_tag = None
        base_version = read_version_file(repo / "VERSION")

    commits = commits_since(repo, base_tag)
    bump = classify_bump(commits)
    if bump is None:
        if not allow_empty:
            raise RuntimeError("No commits found since last stable tag.")
        bump = "patch"

    next_base = bump_version(base_version, bump)
    if channel != "stable":
        build = next_build_for_base(tags, next_base)
        return next_base + [build]
    return next_base


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compute the next PakFu version from git history.",
    )
    parser.add_argument(
        "--channel",
        choices=["stable", "beta", "dev"],
        default="stable",
        help="Release channel. Beta/dev produce a numeric prerelease build.",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="Write the computed version to VERSION.",
    )
    parser.add_argument(
        "--allow-empty",
        action="store_true",
        help="Allow version bumps even if no commits were found.",
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    version_parts = compute_next_version(repo, args.channel, args.allow_empty)
    version = format_version(version_parts)

    if args.write:
        (repo / "VERSION").write_text(version + "\n", encoding="utf-8")

    print(version)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
