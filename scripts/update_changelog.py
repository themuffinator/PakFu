#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

SECTION_ORDER = [
    "Breaking Changes",
    "Highlights",
    "Preview and Format Support",
    "Reliability and Polish",
    "Installers and Updates",
    "Compatibility",
]

SUBJECT_RE = re.compile(r"^(?P<type>[a-zA-Z]+)(\(.+\))?(?P<bang>!)?:\s*(?P<desc>.+)")

TECHNICAL_ONLY_TYPES = {"build", "ci", "chore", "docs", "style", "test"}
TECHNICAL_ONLY_PATTERNS = (
    "action",
    "actions",
    "artifact",
    "artifacts",
    "changelog",
    "ci",
    "docs",
    "documentation",
    "funding",
    "github",
    "release automation",
    "test",
    "tests",
    "workflow",
    "workflows",
)

USER_IMPACT_KEYWORDS = (
    "appimage",
    "archive",
    "asset",
    "audio",
    "batch",
    "bsp",
    "cin",
    "cli",
    "convert",
    "dialog",
    "download",
    "extension",
    "extract",
    "file",
    "filetype",
    "format",
    "folder",
    "image",
    "import",
    "install",
    "installer",
    "linux",
    "macos",
    "memory",
    "model",
    "m8",
    "bk",
    "fm",
    "opengl",
    "package",
    "pak",
    "palette",
    "performance",
    "plugin",
    "portable",
    "proc",
    "preview",
    "roq",
    "runtime",
    "search",
    "shell",
    "updater",
    "video",
    "viewer",
    "vulkan",
    "wad",
    "windows",
    "zip",
)

FORMAT_SUPPORT_KEYWORDS = (
    "asset support",
    "filetype support",
    "format support",
    "heretic",
    "heretic ii",
    "m8",
    "bk",
    "fm",
    " os ",
    "src/formats/m8_image",
    "src/formats/idtech_asset_loader",
    "src/formats/model.cpp",
    "src/formats/sprite_loader.cpp",
)

IDTECH4_MAP_KEYWORDS = (
    "idtech4",
    "idtech4_map",
    ".proc",
    " proc ",
    ".map",
    "compiled render",
)

EXTENSION_WRITEBACK_KEYWORDS = (
    "entries.import",
    "write-back",
    "write back",
    "capability",
    "capabilities",
    "capability negotiation",
    "extension_plugin",
    "docs/extensions.md",
)

CORE_LIBRARY_KEYWORDS = (
    "pakfu_core",
    "core_library",
    "pkg-config",
    "public header",
    "public headers",
)

SHELL_INTEGRATION_KEYWORDS = (
    "file association",
    "file associations",
    "shell integration",
    "shell-open",
    "mime",
    "metainfo",
    "desktop",
    "info.plist",
    "open with",
)

PREVIEW_CONTEXT_KEYWORDS = (
    "asset context",
    "palette provenance",
    "companion",
    "dependency resolution",
    "preview fallback",
    "fallback notes",
    "preview_pane",
    "image_viewer_window",
    "model_viewer_window",
)

PREVIEW_PERFORMANCE_KEYWORDS = (
    "heavy preview",
    "timing profile",
    "timing profiles",
    "cached temp",
    "temp exports",
    "cap bsp",
    "texture resolution",
    "performance",
    "memory",
)


@dataclass(frozen=True)
class CommitChange:
    subject: str
    body: str
    files: tuple[str, ...] = ()


def contains_any(text: str, needles: tuple[str, ...]) -> bool:
    return any(needle in text for needle in needles)


def run_git(args: list[str], cwd: Path) -> str:
    return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()


def last_tag(repo: Path) -> str | None:
    try:
        return run_git(["describe", "--tags", "--abbrev=0"], repo)
    except subprocess.CalledProcessError:
        return None


def commit_log(repo: Path, base_tag: str | None) -> list[CommitChange]:
    if base_tag:
        range_spec = f"{base_tag}..HEAD"
    else:
        range_spec = "HEAD"
    log = run_git(["log", range_spec, "--pretty=format:%H%n%s%n%b%n==END=="], repo)
    entries = [entry.strip() for entry in log.split("==END==") if entry.strip()]
    commits = []
    for entry in entries:
        lines = entry.splitlines()
        commit_hash = lines[0].strip() if lines else ""
        subject = lines[1].strip() if len(lines) > 1 else ""
        body = "\n".join(lines[2:]) if len(lines) > 2 else ""
        files = tuple(
            line.strip()
            for line in run_git(["show", "--format=", "--name-only", commit_hash], repo).splitlines()
            if line.strip()
        )
        commits.append(CommitChange(subject=subject, body=body, files=files))
    return commits


def normalize_subject(subject: str) -> tuple[str | None, str, bool]:
    if subject.startswith("Merge "):
        return None, "", False
    if subject.startswith("chore(release):"):
        return None, "", False
    match = SUBJECT_RE.match(subject)
    if match:
        change_type = match.group("type").lower()
        desc = match.group("desc").strip()
        breaking = match.group("bang") == "!"
        return change_type, desc, breaking
    return None, subject.strip(), False


def should_skip_change(change_type: str | None, desc: str) -> bool:
    lowered = desc.lower()
    if not lowered:
        return True
    if change_type in TECHNICAL_ONLY_TYPES and not contains_any(lowered, USER_IMPACT_KEYWORDS):
        return True
    if lowered.startswith(("trigger ", "test ")):
        return True
    if contains_any(lowered, TECHNICAL_ONLY_PATTERNS) and not contains_any(lowered, USER_IMPACT_KEYWORDS):
        return True
    return False


def add_once(sections: dict[str, list[str]], title: str, item: str) -> None:
    items = sections.setdefault(title, [])
    if item not in items:
        items.append(item)


def normalize_change(commit: CommitChange | tuple[str, str] | tuple[str, str, tuple[str, ...]]) -> CommitChange:
    if isinstance(commit, CommitChange):
        return commit
    if len(commit) >= 3:
        return CommitChange(subject=commit[0], body=commit[1], files=tuple(commit[2]))
    return CommitChange(subject=commit[0], body=commit[1])


def classify_user_change(
    change_type: str | None,
    desc: str,
    body: str = "",
    files: tuple[str, ...] = (),
) -> list[tuple[str, str]]:
    lowered = desc.lower()
    context = " ".join([desc, body, *files]).lower()
    if should_skip_change(change_type, desc) and not contains_any(context, USER_IMPACT_KEYWORDS):
        return []

    items: list[tuple[str, str]] = []

    if contains_any(context, FORMAT_SUPPORT_KEYWORDS):
        items.append(
            (
                "Compatibility",
                "Heretic II filetype support has been added, including M8 textures, FM models, BK book sprites, OS script bytecode previews, and related file associations.",
            )
        )
    if contains_any(context, IDTECH4_MAP_KEYWORDS):
        items.append(
            (
                "Preview and Format Support",
                "idTech4 map-era files are clearer to inspect: `.map` source files and `.proc` render descriptions open as text or metadata, while 3D rendering remains scoped to Quake-family BSP files.",
            )
        )
    if contains_any(context, EXTENSION_WRITEBACK_KEYWORDS):
        items.append(
            (
                "Highlights",
                "Extension commands now support capability negotiation and can import generated files back into editable archives through host-validated write-back.",
            )
        )
    if contains_any(context, CORE_LIBRARY_KEYWORDS):
        items.append(
            (
                "Highlights",
                "`pakfu_core` is easier to reuse from helper tools, with public headers and package metadata for non-UI archive and asset workflows.",
            )
        )
    if contains_any(context, SHELL_INTEGRATION_KEYWORDS):
        items.append(
            (
                "Installers and Updates",
                "Desktop integration is broader across platforms, with improved file associations, Linux MIME metadata, macOS document types, and shell-open handling.",
            )
        )
    if contains_any(context, PREVIEW_CONTEXT_KEYWORDS):
        items.append(
            (
                "Reliability and Polish",
                "Preview panes now surface more asset context, including palette sources, companion-file resolution, renderer state, and fallback notes.",
            )
        )
    if contains_any(context, PREVIEW_PERFORMANCE_KEYWORDS):
        items.append(
            (
                "Reliability and Polish",
                "Heavy previews are more controlled, with clearer timing information, reusable temporary exports, and safer limits for large map texture work.",
            )
        )

    if contains_any(lowered, ("extension", "plugin", "manifest")):
        items.append(
            (
                "Highlights",
                "Extension commands are easier to discover and run from both the app and the CLI.",
            )
        )
    if contains_any(lowered, ("search", "index")) and "archive" in lowered:
        items.append(
            (
                "Highlights",
                "Archive search and CLI workflows are more capable, making large archives easier to inspect and filter.",
            )
        )
    if contains_any(lowered, ("batch", "convert", "conversion", "image writer")):
        items.append(
            (
                "Highlights",
                "Batch conversion supports more useful output formats with clearer format-specific options.",
            )
        )
    if contains_any(lowered, ("vulkan", "opengl", "3d preview", "renderer")):
        items.append(
            (
                "Preview and Format Support",
                "3D previews are more portable, with Vulkan treated as optional and OpenGL kept available as the fallback renderer.",
            )
        )
    if contains_any(lowered, ("model", "bsp", "cinematic", "roq", "video", "audio", "image", "palette")):
        items.append(
            (
                "Preview and Format Support",
                "Preview and format handling has been broadened for more idTech-era assets.",
            )
        )
    if contains_any(lowered, ("dialog", "hang", "stale", "folder", "path", "crash", "memory", "performance", "responsive")):
        items.append(
            (
                "Reliability and Polish",
                "Everyday browsing and file handling are smoother, with fixes aimed at responsiveness and stale navigation state.",
            )
        )
    if contains_any(
        lowered,
        (
            "linux",
            "windows",
            "macos",
            "package",
            "packaging",
            "installer",
            "appimage",
            "portable",
            "runtime",
            "update",
            "updater",
        ),
    ):
        items.append(
            (
                "Installers and Updates",
                "Release packages are more reliable and self-contained, so downloads are easier to install and verify.",
            )
        )
    if contains_any(lowered, ("format", "archive", "pak", "wad", "zip", "pk3", "resources")):
        items.append(
            (
                "Compatibility",
                "Archive compatibility has been expanded and tightened across supported game formats.",
            )
        )

    if not items and change_type in {"feat", "fix", "perf"}:
        cleaned = desc[0].upper() + desc[1:] if desc else desc
        items.append(("Highlights", cleaned.rstrip(".") + "."))
    return items


def build_sections(commits: list[CommitChange | tuple[str, str] | tuple[str, str, tuple[str, ...]]]) -> dict[str, list[str]]:
    sections: dict[str, list[str]] = {}
    for raw_commit in commits:
        commit = normalize_change(raw_commit)
        subject = commit.subject
        body = commit.body
        change_type, desc, breaking = normalize_subject(subject)
        if not desc:
            continue
        if "BREAKING CHANGE" in body:
            breaking = True
        if breaking:
            add_once(sections, "Breaking Changes", desc)
            continue
        for title, item in classify_user_change(change_type, desc, body, commit.files):
            add_once(sections, title, item)
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
    date = args.date or dt.datetime.now(dt.UTC).date().isoformat()

    base_tag = last_tag(repo)
    commits = commit_log(repo, base_tag)
    sections = build_sections(commits)

    entry = [f"## [{version}] - {date}"]
    if not sections:
        entry += ["### Highlights", "- No user-facing changes in this build."]
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
