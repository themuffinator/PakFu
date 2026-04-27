#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import re
import shutil
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Page:
    filename: str
    title: str
    source_sections: tuple[str, ...]


PAGES = (
    Page("index.html", "Welcome", ("__intro__", "Overview", "Download", "Highlights")),
    Page("formats.html", "Supported Formats", ("Supported Formats",)),
    Page("cli.html", "CLI Quick Reference", ("CLI Quick Reference",)),
    Page(
        "workflows.html",
        "Using PakFu",
        ("GUI Workflow", "Installations (Game Profiles)", "Updates and Releases", "Environment Variables"),
    ),
    Page("credits.html", "Credits and License", ("Credits", "License")),
)

CSS = """
:root {
  color-scheme: light dark;
  --bg: #f6f4ef;
  --panel: #fffefa;
  --text: #1d252c;
  --muted: #5c6670;
  --line: #d8d2c6;
  --accent: #0a66c2;
  --accent-strong: #084f96;
  --code: #20242a;
  --code-bg: #eef3f8;
}

@media (prefers-color-scheme: dark) {
  :root {
    --bg: #151719;
    --panel: #202326;
    --text: #f2f0ea;
    --muted: #b6b0a6;
    --line: #373c41;
    --accent: #72b7ff;
    --accent-strong: #a9d4ff;
    --code: #f2f0ea;
    --code-bg: #111416;
  }
}

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  background: var(--bg);
  color: var(--text);
  font: 16px/1.6 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
}

a {
  color: var(--accent);
}

.shell {
  max-width: 1180px;
  margin: 0 auto;
  padding: 32px 24px 48px;
}

.masthead {
  display: flex;
  align-items: center;
  gap: 18px;
  padding-bottom: 20px;
  border-bottom: 1px solid var(--line);
}

.logo {
  width: 96px;
  height: auto;
}

.eyebrow {
  margin: 0;
  color: var(--muted);
  font-size: 0.92rem;
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

h1,
h2,
h3 {
  line-height: 1.18;
  letter-spacing: 0;
}

h1 {
  margin: 4px 0 0;
  font-size: clamp(2.1rem, 4vw, 3.4rem);
}

h2 {
  margin-top: 34px;
  padding-top: 20px;
  border-top: 1px solid var(--line);
  font-size: 1.7rem;
}

h3 {
  margin-top: 26px;
  font-size: 1.22rem;
}

.layout {
  display: grid;
  grid-template-columns: 220px minmax(0, 1fr);
  gap: 34px;
  align-items: start;
}

.nav {
  position: sticky;
  top: 18px;
  margin-top: 28px;
  padding: 14px;
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 8px;
}

.nav a {
  display: block;
  padding: 8px 10px;
  border-radius: 6px;
  color: var(--text);
  text-decoration: none;
}

.nav a[aria-current="page"] {
  background: var(--accent);
  color: white;
}

.content {
  min-width: 0;
}

.content > p:first-child {
  font-size: 1.08rem;
}

ul {
  padding-left: 1.2rem;
}

.guide-list {
  padding-left: 0;
  list-style: none;
}

.guide-list li {
  position: relative;
  margin: 8px 0;
  padding-left: 22px;
}

.guide-list li::before {
  content: "";
  position: absolute;
  left: 2px;
  top: 0.72em;
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--accent);
}

.depth-1 {
  margin-left: 22px !important;
}

.depth-2,
.depth-3 {
  margin-left: 44px !important;
}

code {
  color: var(--code);
  background: var(--code-bg);
  border-radius: 5px;
  padding: 0.12em 0.34em;
  font-size: 0.92em;
}

pre {
  overflow-x: auto;
  padding: 16px;
  background: var(--code-bg);
  border: 1px solid var(--line);
  border-radius: 8px;
}

pre code {
  padding: 0;
  background: transparent;
}

.table-wrap {
  overflow-x: auto;
  margin: 18px 0;
  border: 1px solid var(--line);
  border-radius: 8px;
  background: var(--panel);
}

table {
  width: 100%;
  border-collapse: collapse;
  min-width: 620px;
}

th,
td {
  padding: 10px 12px;
  border-bottom: 1px solid var(--line);
  text-align: left;
  vertical-align: top;
}

th {
  color: var(--accent-strong);
  background: color-mix(in srgb, var(--accent) 10%, transparent);
}

tr:last-child td {
  border-bottom: 0;
}

.footer {
  margin-top: 42px;
  color: var(--muted);
  font-size: 0.9rem;
}

@media (max-width: 760px) {
  .shell {
    padding: 22px 16px 34px;
  }

  .masthead {
    align-items: flex-start;
  }

  .logo {
    width: 72px;
  }

  .layout {
    display: block;
  }

  .nav {
    position: static;
  }
}
""".strip()


def slugify(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return slug or "section"


def extract_intro(readme: str) -> str:
    lines = readme.splitlines()
    start = None
    for idx, line in enumerate(lines):
        if line.startswith("PakFu is a cross-platform"):
            start = idx
            break
    if start is None:
        return "PakFu is a cross-platform archive manager and asset viewer."

    collected = []
    for line in lines[start:]:
        if line.startswith("<details>") or line.startswith("## "):
            break
        if line.strip():
            collected.append(line)
    return "\n\n".join(collected)


def extract_sections(readme: str) -> dict[str, str]:
    sections: dict[str, list[str]] = {}
    current_title: str | None = None
    current_lines: list[str] = []
    for line in readme.splitlines():
        if line.startswith("## "):
            if current_title:
                sections[current_title] = current_lines
            current_title = line[3:].strip()
            current_lines = [line]
            continue
        if current_title:
            current_lines.append(line)
    if current_title:
        sections[current_title] = current_lines
    return {title: "\n".join(lines).strip() for title, lines in sections.items()}


def filter_section(title: str, markdown: str, version: str) -> str:
    lines = markdown.splitlines()
    filtered: list[str] = []

    if title == "Overview":
        for line in lines:
            if line.startswith("- Build system:") or line.startswith("- UI framework:"):
                continue
            if line.startswith("- Current version:"):
                filtered.append(f"- Current version: `{version}`.")
                continue
            filtered.append(line)
        return "\n".join(filtered).strip()

    if title == "Highlights":
        return "\n".join(line for line in lines if "`pakfu_core`" not in line).strip()

    if title == "Supported Formats":
        return "\n".join(
            line for line in lines if "docs/SUPPORT_MATRIX.md" not in line and "fixture-backed support contract" not in line
        ).strip()

    if title == "Updates and Releases":
        for line in lines:
            if line.startswith("Build-time updater config:"):
                break
            filtered.append(line)
        return "\n".join(filtered).strip()

    return markdown.strip()


def render_inline(value: str) -> str:
    parts = re.split(r"(`[^`]*`)", value)
    rendered: list[str] = []
    for part in parts:
        if part.startswith("`") and part.endswith("`"):
            rendered.append(f"<code>{html.escape(part[1:-1])}</code>")
        else:
            rendered.append(render_links_and_emphasis(part))
    return "".join(rendered)


def render_links_and_emphasis(value: str) -> str:
    output = []
    pos = 0
    for match in re.finditer(r"\[([^\]]+)\]\(([^)]+)\)", value):
        output.append(html.escape(value[pos : match.start()]))
        label = html.escape(match.group(1))
        href = html.escape(match.group(2), quote=True)
        output.append(f'<a href="{href}">{label}</a>')
        pos = match.end()
    output.append(html.escape(value[pos:]))
    text = "".join(output)
    return re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)


def split_table_row(line: str) -> list[str]:
    stripped = line.strip().strip("|")
    return [cell.strip() for cell in stripped.split("|")]


def is_table_separator(line: str) -> bool:
    return bool(re.match(r"^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)+\|?\s*$", line))


def render_table(lines: list[str]) -> str:
    header = split_table_row(lines[0])
    body_rows = [split_table_row(line) for line in lines[2:]]
    out = ["<div class=\"table-wrap\"><table>", "<thead><tr>"]
    out.extend(f"<th>{render_inline(cell)}</th>" for cell in header)
    out.extend(["</tr></thead>", "<tbody>"])
    for row in body_rows:
        out.append("<tr>")
        out.extend(f"<td>{render_inline(cell)}</td>" for cell in row)
        out.append("</tr>")
    out.extend(["</tbody>", "</table></div>"])
    return "\n".join(out)


def render_markdown(markdown: str) -> str:
    lines = markdown.splitlines()
    out: list[str] = []
    paragraph: list[str] = []
    in_list = False
    idx = 0

    def close_paragraph() -> None:
        nonlocal paragraph
        if paragraph:
            out.append(f"<p>{render_inline(' '.join(paragraph))}</p>")
            paragraph = []

    def close_list() -> None:
        nonlocal in_list
        if in_list:
            out.append("</ul>")
            in_list = False

    while idx < len(lines):
        line = lines[idx]
        stripped = line.strip()

        if not stripped:
            close_paragraph()
            close_list()
            idx += 1
            continue

        if stripped.startswith("```"):
            close_paragraph()
            close_list()
            language = stripped[3:].strip()
            code_lines = []
            idx += 1
            while idx < len(lines) and not lines[idx].strip().startswith("```"):
                code_lines.append(lines[idx])
                idx += 1
            idx += 1
            class_attr = f' class="language-{html.escape(language, quote=True)}"' if language else ""
            out.append(f"<pre><code{class_attr}>{html.escape(chr(10).join(code_lines))}</code></pre>")
            continue

        if idx + 1 < len(lines) and "|" in line and is_table_separator(lines[idx + 1]):
            close_paragraph()
            close_list()
            table_lines = [line, lines[idx + 1]]
            idx += 2
            while idx < len(lines) and "|" in lines[idx] and lines[idx].strip():
                table_lines.append(lines[idx])
                idx += 1
            out.append(render_table(table_lines))
            continue

        heading = re.match(r"^(#{2,4})\s+(.+)$", line)
        if heading:
            close_paragraph()
            close_list()
            level = min(len(heading.group(1)), 3)
            text = heading.group(2).strip()
            out.append(f'<h{level} id="{slugify(text)}">{render_inline(text)}</h{level}>')
            idx += 1
            continue

        item = re.match(r"^(\s*)-\s+(.+)$", line)
        if item:
            close_paragraph()
            if not in_list:
                out.append('<ul class="guide-list">')
                in_list = True
            depth = min(len(item.group(1)) // 2, 3)
            out.append(f'<li class="depth-{depth}">{render_inline(item.group(2))}</li>')
            idx += 1
            continue

        close_list()
        paragraph.append(stripped)
        idx += 1

    close_paragraph()
    close_list()
    return "\n".join(out)


def page_template(page: Page, body: str, version: str, has_logo: bool) -> str:
    nav_links = []
    for entry in PAGES:
        current = ' aria-current="page"' if entry.filename == page.filename else ""
        nav_links.append(f'<a href="{entry.filename}"{current}>{html.escape(entry.title)}</a>')
    nav = "\n".join(nav_links)
    logo = '<img class="logo" src="media/pakfu-logo.png" alt="PakFu logo">' if has_logo else ""
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="generator" content="scripts/build_user_guide.py">
  <title>{html.escape(page.title)} - PakFu Guide</title>
  <link rel="stylesheet" href="styles.css">
</head>
<body>
  <div class="shell">
    <header class="masthead">
      {logo}
      <div>
        <p class="eyebrow">PakFu {html.escape(version)} user guide</p>
        <h1>{html.escape(page.title)}</h1>
      </div>
    </header>
    <div class="layout">
      <nav class="nav" aria-label="Guide pages">
        {nav}
      </nav>
      <main class="content">
        {body}
        <p class="footer">Generated from the user-facing sections of README.md.</p>
      </main>
    </div>
  </div>
</body>
</html>
"""


def build_pages(readme: str, output: Path, version: str, repo_root: Path) -> None:
    sections = extract_sections(readme)
    content = {"__intro__": extract_intro(readme)}
    for title, markdown in sections.items():
        content[title] = filter_section(title, markdown, version)

    output.mkdir(parents=True, exist_ok=True)
    media_dir = output / "media"
    media_dir.mkdir(parents=True, exist_ok=True)
    logo_source = repo_root / "assets" / "img" / "logo.png"
    has_logo = logo_source.exists()
    if has_logo:
        shutil.copyfile(logo_source, media_dir / "pakfu-logo.png")

    (output / "styles.css").write_text(CSS + "\n", encoding="utf-8")
    for page in PAGES:
        chunks = [content[name] for name in page.source_sections if content.get(name)]
        body = render_markdown("\n\n".join(chunks))
        (output / page.filename).write_text(
            page_template(page, body, version, has_logo),
            encoding="utf-8",
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the packaged PakFu HTML user guide.")
    parser.add_argument("--readme", default="README.md", help="README source file.")
    parser.add_argument("--output", required=True, help="Output directory for HTML files.")
    parser.add_argument("--version", help="PakFu version to show in the guide.")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    readme_path = Path(args.readme)
    if not readme_path.is_absolute():
        readme_path = repo_root / readme_path
    output = Path(args.output)
    if not output.is_absolute():
        output = repo_root / output

    version = args.version
    if not version:
        version_file = repo_root / "VERSION"
        version = version_file.read_text(encoding="utf-8").strip() if version_file.exists() else "development"

    build_pages(readme_path.read_text(encoding="utf-8"), output, version, repo_root)
    print(f"Wrote PakFu user guide: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
