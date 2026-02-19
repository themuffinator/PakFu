#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import subprocess
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
    return ".".join(str(part) for part in parts)


def read_version_base(path: Path) -> list[int]:
    raw = path.read_text(encoding="utf-8").strip()
    parts = parse_version(raw)
    if not parts:
        raise RuntimeError(f"VERSION is not numeric: {raw!r}")
    return parts[:3]


def list_tags(repo: Path) -> list[tuple[str, list[int]]]:
    tags_raw = run_git(["tag", "--list", "v*"], cwd=repo)
    tags: list[tuple[str, list[int]]] = []
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


def latest_nightly_tag(tags: list[tuple[str, list[int]]]) -> tuple[str, list[int]] | None:
    nightly = [(tag, parts) for tag, parts in tags if len(parts) == 4]
    if not nightly:
        return None
    return max(nightly, key=lambda item: tuple(item[1]))


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


def classify_bump(commits: list[tuple[str, str]]) -> str:
    if not commits:
        return "patch"
    bump = "patch"
    for subject, body in commits:
        if BREAKING_RE.match(subject) or "BREAKING CHANGE" in body:
            return "major"
        if FEATURE_RE.match(subject):
            bump = "minor"
    return bump


def is_meaningful_commit(subject: str) -> bool:
    lowered = subject.strip().lower()
    if lowered.startswith("chore(release):"):
        return False
    return True


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


def compute_nightly_version(repo: Path) -> tuple[str, str, str, int, str, str]:
    tags = list_tags(repo)
    stable = latest_stable_tag(tags)
    nightly = latest_nightly_tag(tags)

    if stable:
        stable_tag, stable_version = stable
    else:
        stable_tag = ""
        stable_version = read_version_base(repo / "VERSION")

    bump_commits = commits_since(repo, stable_tag or None)
    bump = classify_bump(bump_commits)
    next_base = bump_version(stable_version, bump)
    build = next_build_for_base(tags, next_base)
    version = format_version(next_base + [build])

    latest_nightly_tag_name = nightly[0] if nightly else ""
    commits_after_nightly = commits_since(repo, latest_nightly_tag_name or None)
    meaningful_after_nightly = [
        commit for commit in commits_after_nightly if is_meaningful_commit(commit[0])
    ]
    commits_since_nightly = len(meaningful_after_nightly)
    head_sha = run_git(["rev-parse", "HEAD"], cwd=repo)

    return (
        version,
        stable_tag,
        latest_nightly_tag_name,
        commits_since_nightly,
        head_sha,
        format_version(next_base),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Compute nightly version and release decision.")
    parser.add_argument(
        "--force",
        action="store_true",
        help="Force release even when no commits since previous nightly.",
    )
    parser.add_argument(
        "--write",
        metavar="PATH",
        help="Optional VERSION file path to write the computed version.",
    )
    parser.add_argument(
        "--format",
        choices=["text", "json", "github"],
        default="text",
        help="Output format.",
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    version, stable_tag, nightly_tag, commit_count, head_sha, base_version = compute_nightly_version(repo)
    should_release = args.force or commit_count > 0
    payload = {
        "version": version,
        "tag": f"v{version}",
        "base_version": base_version,
        "latest_stable_tag": stable_tag,
        "latest_nightly_tag": nightly_tag,
        "commits_since_nightly": str(commit_count),
        "head_sha": head_sha,
        "should_release": "true" if should_release else "false",
    }

    if args.write:
        Path(args.write).write_text(version + "\n", encoding="utf-8")

    if args.format == "json":
        print(json.dumps(payload, indent=2))
        return 0
    if args.format == "github":
        for key, value in payload.items():
            print(f"{key}={value}")
        return 0

    print(version)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
