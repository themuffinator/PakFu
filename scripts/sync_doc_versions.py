#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

VERSION_RE = re.compile(r"^\d+(?:\.\d+){2,3}$")
VERSION_BADGE_RE = re.compile(
    r"(badge/version-)(\d+(?:\.\d+){2,3})(-[^?\"')\s]+(?=\?style=for-the-badge))"
)
CURRENT_VERSION_RE = re.compile(
    r"(- Current version:\s*`)(\d+(?:\.\d+){2,3})(`[^\r\n]*)"
)


def read_version(repo: Path, version: str | None) -> str:
    resolved = version or (repo / "VERSION").read_text(encoding="utf-8").strip()
    if not VERSION_RE.match(resolved):
        raise RuntimeError(
            f"Version must be numeric dot-segments with 3 or 4 parts: {resolved!r}"
        )
    return resolved


def markdown_docs(repo: Path) -> list[Path]:
    docs_dir = repo / "docs"
    paths = [repo / "README.md"]
    if docs_dir.exists():
        paths += sorted(docs_dir.rglob("*.md"))
    return paths


def update_text(text: str, version: str, require_readme_markers: bool) -> tuple[str, list[str]]:
    errors: list[str] = []

    text, badge_count = VERSION_BADGE_RE.subn(
        lambda match: f"{match.group(1)}{version}{match.group(3)}",
        text,
    )
    if require_readme_markers and badge_count == 0:
        errors.append("README.md version badge was not found.")

    text, current_count = CURRENT_VERSION_RE.subn(
        lambda match: f"{match.group(1)}{version}{match.group(3)}",
        text,
    )
    if require_readme_markers and current_count == 0:
        errors.append("README.md current-version declaration was not found.")

    return text, errors


def sync_doc_versions(repo: Path, version: str | None = None, check: bool = False) -> list[Path]:
    resolved_version = read_version(repo, version)
    changed: list[Path] = []
    errors: list[str] = []

    for path in markdown_docs(repo):
        if not path.exists():
            if path.name == "README.md":
                errors.append("README.md was not found.")
            continue

        original = path.read_bytes().decode("utf-8")
        updated, path_errors = update_text(
            original,
            resolved_version,
            require_readme_markers=path.name == "README.md",
        )
        errors += path_errors

        if updated != original:
            changed.append(path)
            if not check:
                path.write_bytes(updated.encode("utf-8"))

    if errors:
        raise RuntimeError("\n".join(errors))

    if check and changed:
        names = ", ".join(path.relative_to(repo).as_posix() for path in changed)
        raise RuntimeError(
            "Documentation version declarations are stale: "
            f"{names}. Run `python scripts/sync_doc_versions.py`."
        )

    return changed


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Keep documentation version declarations aligned with VERSION.",
    )
    parser.add_argument(
        "--repo",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="Repository root. Defaults to this script's parent repository.",
    )
    parser.add_argument(
        "--version",
        help="Version to write. Defaults to the repository VERSION file.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Validate without writing files.",
    )
    args = parser.parse_args()

    repo = args.repo.resolve()
    try:
        changed = sync_doc_versions(repo, args.version, args.check)
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 1

    if changed:
        action = "Would update" if args.check else "Updated"
        for path in changed:
            print(f"{action} {path.relative_to(repo).as_posix()}")
    elif not args.check:
        print("Documentation version declarations already up to date.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
