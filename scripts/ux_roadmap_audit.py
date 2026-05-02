#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


TEXT_EXTENSIONS = {
    ".cpp",
    ".h",
    ".hpp",
    ".cc",
    ".cxx",
    ".md",
    ".py",
    ".build",
    ".txt",
}

IGNORED_PARTS = {
    ".git",
    "builddir",
    "builddir_ci_test2",
    "builddir_idwav_test",
    "builddir-fuzz",
    "dist",
    "install",
    "squashfs-root",
    "third_party",
    "__pycache__",
}


@dataclass(frozen=True)
class Finding:
    status: str
    area: str
    message: str
    evidence: list[str]
    required: bool = False


def iter_text_files(root: Path, paths: list[str]) -> list[Path]:
    files: list[Path] = []
    for rel in paths:
        base = root / rel
        if not base.exists():
            continue
        if base.is_file() and base.suffix in TEXT_EXTENSIONS:
            files.append(base)
            continue
        for path in base.rglob("*"):
            if any(part in IGNORED_PARTS for part in path.relative_to(root).parts):
                continue
            if path.is_file() and path.suffix in TEXT_EXTENSIONS:
                files.append(path)
    return sorted(files)


def read_file(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return path.read_text(encoding="utf-8", errors="replace")


def rel(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def grep(files: list[Path], root: Path, pattern: str) -> list[str]:
    regex = re.compile(pattern)
    hits: list[str] = []
    for path in files:
        for number, line in enumerate(read_file(path).splitlines(), start=1):
            if regex.search(line):
                hits.append(f"{rel(path, root)}:{number}: {line.strip()}")
    return hits


def grep_blocks(files: list[Path], root: Path, start_pattern: str, block_pattern: str, max_lines: int = 12) -> list[str]:
    start_regex = re.compile(start_pattern)
    block_regex = re.compile(block_pattern, re.DOTALL)
    hits: list[str] = []
    for path in files:
        lines = read_file(path).splitlines()
        number = 0
        while number < len(lines):
            line = lines[number]
            if not start_regex.search(line):
                number += 1
                continue
            block_lines = [line]
            end = number
            while ";" not in "\n".join(block_lines) and end + 1 < len(lines) and end - number < max_lines:
                end += 1
                block_lines.append(lines[end])
            block = "\n".join(block_lines)
            if block_regex.search(block):
                hits.append(f"{rel(path, root)}:{number + 1}: {line.strip()}")
            number = end + 1
    return hits


def noncritical_information_dialog_hits(files: list[Path], root: Path) -> list[str]:
    return grep_blocks(
        files,
        root,
        r"QMessageBox::information|QMessageBox\s+(?:\w+\s*)?\(|QMessageBox\s*\(|setIcon\s*\(",
        r"QMessageBox::information\s*\(|QMessageBox\s+(?:\w+\s*)?\([^;]*QMessageBox::Information|QMessageBox\s*\([^;]*QMessageBox::Information|setIcon\s*\([^;]*QMessageBox::Information",
        max_lines=16,
    )


def unsafe_performance_trace_hits(files: list[Path], root: Path) -> list[str]:
    sensitive_pattern = re.compile(
        r"(ScopedTimer\s*\([^;\n]*(?:fileName|baseName|completeBaseName|absolutePath|absoluteFilePath|path_|pak_path|source_path|dest_path|shader_path|archivePath|filePath|g_crash_dir|crash_dir|session_log_path)"
        r"|set_detail\s*\([^;\n]*(?:fileName|baseName|completeBaseName|absolutePath|absoluteFilePath|path_|source_path|dest_path|shader_path|archivePath|filePath|g_crash_dir|crash_dir|session_log_path))"
    )
    qt_log_start_pattern = re.compile(r"\bqC?(?:Debug|Info|Warning|Critical)\s*\(")
    qt_log_sensitive_pattern = re.compile(
        r"(absoluteFilePath|absolutePath|toLocalFile|QDir::toNativeSeparators|file_path|source_path|dest_path|pak_path|shader_path|archivePath|filePath|g_crash_dir|crash_dir|session_log_path|path\s*=%|path=|<<\s*(?:path|shader_path|archivePath|filePath|g_crash_dir|crash_dir|session_log_path)\b)"
    )
    allowlist = (
        "preview_metric_detail(",
        "archive_metric_detail(",
    )
    hits: list[str] = []
    for path in files:
        lines = read_file(path).splitlines()
        number = 0
        while number < len(lines):
            line = lines[number]
            line_number = number + 1
            if any(token in line for token in allowlist):
                number += 1
                continue
            if sensitive_pattern.search(line):
                hits.append(f"{rel(path, root)}:{line_number}: {line.strip()}")
            if qt_log_start_pattern.search(line):
                block_lines = [line]
                end = number
                while ";" not in "\n".join(block_lines) and end + 1 < len(lines) and end - number < 8:
                    end += 1
                    block_lines.append(lines[end])
                block = "\n".join(block_lines)
                if qt_log_sensitive_pattern.search(block):
                    hits.append(f"{rel(path, root)}:{line_number}: {line.strip()}")
                number = end + 1
                continue
            number += 1
    return hits


def raw_user_visible_literal_hits(files: list[Path], root: Path) -> list[str]:
    call_pattern = re.compile(
        r"\b(?:setToolTip|setStatusTip|setAccessibleName|setAccessibleDescription|setWindowTitle|setStatusText|setText|addButton)\s*\(\s*\"",
        re.DOTALL,
    )
    constructor_pattern = re.compile(
        r"\b(?:new\s+)?(?:QLabel|QPushButton|QCheckBox|QGroupBox)\s*\(\s*\"(?P<literal>[^\"\\]*(?:\\.[^\"\\]*)*)\"",
        re.DOTALL,
    )
    sample_literals = (
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "abcdefghijklmnopqrstuvwxyz",
        "0123456789",
        "!\\\"#$%&'()*+,-./:;<=>?@[\\\\]^_`{|}~",
    )
    constructor_allowlist = {
        "",
        ">",
        "...",
        "0/0",
    }
    hits: list[str] = []
    for path in files:
        lines = read_file(path).splitlines()
        number = 0
        while number < len(lines):
            line = lines[number]
            if not re.search(r"\b(?:setToolTip|setStatusTip|setAccessibleName|setAccessibleDescription|setWindowTitle|setStatusText|setText|addButton)\s*\(|\b(?:new\s+)?(?:QLabel|QPushButton|QCheckBox|QGroupBox)\s*\(", line):
                number += 1
                continue
            block_lines = [line]
            end = number
            while ";" not in "\n".join(block_lines) and end + 1 < len(lines) and end - number < 12:
                end += 1
                block_lines.append(lines[end])
            block = "\n".join(block_lines)
            if call_pattern.search(block) and not any(sample in block for sample in sample_literals):
                hits.append(f"{rel(path, root)}:{number + 1}: {line.strip()}")
            for match in constructor_pattern.finditer(block):
                if match.group("literal") not in constructor_allowlist:
                    hits.append(f"{rel(path, root)}:{number + 1}: {line.strip()}")
                    break
            number = end + 1
    return hits


def count_hits(files: list[Path], pattern: str) -> int:
    regex = re.compile(pattern)
    total = 0
    for path in files:
        total += len(regex.findall(read_file(path)))
    return total


def cap(items: list[str], limit: int = 8) -> list[str]:
    if len(items) <= limit:
        return items
    return items[:limit] + [f"... {len(items) - limit} more"]


def check_docs(root: Path) -> list[Finding]:
    required_docs = [
        "docs/proposals/ux-workflow-analysis-1-may-2026.md",
        "docs/UX_VALIDATION.md",
        "docs/ROADMAP_COMPLETION.md",
        "docs/UX_PERFORMANCE_BASELINE.md",
    ]
    evidence = [path for path in required_docs if (root / path).exists()]
    missing = [path for path in required_docs if not (root / path).exists()]
    if missing:
        return [
            Finding(
                "fail",
                "docs",
                "Missing roadmap validation documentation.",
                [f"missing: {path}" for path in missing],
                required=True,
            )
        ]
    return [
        Finding(
            "pass",
            "docs",
            "Roadmap proposal, validation, and completion documents are present.",
            evidence,
            required=True,
        )
    ]


def check_model_view(root: Path, src_files: list[Path]) -> Finding:
    model_hits = grep(src_files, root, r"QAbstract(?:Table|Item|List|Proxy)?Model|QSortFilterProxyModel")
    view_hits = grep(src_files, root, r"\bQ(?:Table|Tree|List)View\b")
    item_widget_hits = grep(src_files, root, r"\bQ(?:Table|Tree|List)Widget\b")
    classified_widget_hits = [
        hit
        for hit in item_widget_hits
        if hit.startswith("src/ui/workspace_tab.")
        or hit.startswith("src/ui/theme_manager.cpp:")
    ]
    scale_sensitive_widget_hits = [
        hit for hit in item_widget_hits if hit not in set(classified_widget_hits)
    ]
    evidence = [
        f"model/model-proxy hits: {len(model_hits)}",
        f"view hits: {len(view_hits)}",
        f"scale-sensitive convenience widget hits: {len(scale_sensitive_widget_hits)}",
        f"classified navigation/style convenience hits: {len(classified_widget_hits)}",
        *cap(model_hits[:4] + view_hits[:4] + scale_sensitive_widget_hits[:4] + classified_widget_hits[:4], 16),
    ]
    if model_hits and view_hits:
        status = "warn" if scale_sensitive_widget_hits else "pass"
        message = (
            "Model/view signals are present; remaining convenience widgets are classified navigation/style helpers."
            if not scale_sensitive_widget_hits
            else "Model/view signals are present; scale-sensitive convenience widgets remain."
        )
    else:
        status = "fail"
        message = "Model/view completion signals were not found."
    return Finding(status, "model_view", message, evidence, required=True)


def check_i18n(root: Path, src_files: list[Path]) -> Finding:
    translator_hits = grep(src_files, root, r"QTranslator|installTranslator")
    translation_hits = grep(src_files, root, r"\btr\s*\(|QCoreApplication::translate")
    raw_literal_hits = raw_user_visible_literal_hits(src_files, root)
    evidence = [
        f"translator bootstrap hits: {len(translator_hits)}",
        f"translation API hits: {len(translation_hits)}",
        f"raw user-visible literal hits: {len(raw_literal_hits)}",
        *cap(raw_literal_hits[:8] + translator_hits[:6] + translation_hits[:6], 20),
    ]
    if raw_literal_hits:
        return Finding(
            "fail",
            "i18n",
            "Raw user-visible UI literals remain outside translation wrappers.",
            evidence,
            required=True,
        )
    if translator_hits and translation_hits:
        return Finding("pass", "i18n", "Translator bootstrap and translation API usage are present.", evidence, required=True)
    return Finding("fail", "i18n", "Translator bootstrap or translation API usage is missing.", evidence, required=True)


def check_accessibility(root: Path, src_files: list[Path]) -> Finding:
    names = grep(src_files, root, r"setAccessibleName")
    descriptions = grep(src_files, root, r"setAccessibleDescription")
    buddies = grep(src_files, root, r"setBuddy")
    evidence = [
        f"accessible name hits: {len(names)}",
        f"accessible description hits: {len(descriptions)}",
        f"label buddy hits: {len(buddies)}",
        *cap(names[:6] + descriptions[:6] + buddies[:4], 14),
    ]
    if len(names) >= 10 and descriptions:
        return Finding("pass", "accessibility", "Accessible names and descriptions are present across major surfaces.", evidence)
    if names:
        return Finding("warn", "accessibility", "Accessible names exist, but semantic coverage needs manual review.", evidence)
    return Finding("fail", "accessibility", "No accessible-name signals found.", evidence, required=True)


def check_modality(root: Path, src_files: list[Path]) -> Finding:
    info_hits = noncritical_information_dialog_hits(src_files, root)
    decision_hits = grep(src_files, root, r"QMessageBox::(?:warning|critical|question)|\.exec\s*\(|\bQDialog\b")
    completion_doc = root / "docs" / "ROADMAP_COMPLETION.md"
    completion_text = read_file(completion_doc).lower() if completion_doc.exists() else ""
    documented_inventory = "modal" in completion_text and "dialog" in completion_text
    evidence = [
        f"noncritical information dialog hits: {len(info_hits)}",
        f"decision/error/dialog hits: {len(decision_hits)}",
        f"completion doc modal classification: {'yes' if documented_inventory else 'no'}",
        *cap(info_hits + decision_hits, 20),
    ]
    if info_hits:
        return Finding(
            "warn",
            "modality",
            "Noncritical information dialogs remain and should be converted to inline status.",
            evidence,
        )
    if not documented_inventory:
        return Finding(
            "warn",
            "modality",
            "Decision/error dialog inventory needs completion-doc classification.",
            evidence,
        )
    return Finding("pass", "modality", "Decision/error dialogs are classified and noncritical information dialogs are gone.", evidence)


def check_audit_regression_fixtures() -> Finding:
    information_icon = "QMessageBox::" + "Information"
    native_separator = "QDir::" + "toNativeSeparators"
    fixture = "\n".join(
        [
            "void modal_fixture() {",
            "  QMessageBox box;",
            "  box.setIcon(",
            f"    {information_icon});",
            f"  QMessageBox direct({information_icon}, \"Title\", \"Body\");",
            "}",
            "",
            "void privacy_fixture() {",
            '  qInfo().noquote() << QString("Crash reporting enabled: %1")',
            f"                    .arg({native_separator}(g_crash_dir));",
            f"  qCInfo(category) << {native_separator}(g_crash_dir);",
            "}",
            "",
            "void i18n_fixture() {",
            "  button->setToolTip(",
            '    "Raw tooltip");',
            '  splash->setStatusText("Starting PakFu...");',
            '  auto* label = new QLabel("Starting...", parent);',
            "}",
        ]
    )
    with tempfile.TemporaryDirectory(prefix="pakfu-ux-audit-") as temp_dir:
        root = Path(temp_dir)
        path = root / "fixture.cpp"
        path.write_text(fixture, encoding="utf-8")
        files = [path]
        modal_hits = noncritical_information_dialog_hits(files, root)
        privacy_hits = unsafe_performance_trace_hits(files, root)
        raw_literal_hits = raw_user_visible_literal_hits(files, root)

    evidence = [
        f"information modal fixture hits: {len(modal_hits)}",
        f"crash directory log fixture hits: {len(privacy_hits)}",
        f"raw visible UI literal fixture hits: {len(raw_literal_hits)}",
        *cap(modal_hits + privacy_hits + raw_literal_hits, 12),
    ]
    if len(modal_hits) >= 2 and len(privacy_hits) >= 2 and len(raw_literal_hits) >= 3:
        return Finding("pass", "audit_regressions", "Static audit regression fixtures catch known false-negative shapes.", evidence, required=True)
    return Finding(
        "fail",
        "audit_regressions",
        "Static audit regression fixtures missed a known false-negative shape.",
        evidence,
        required=True,
    )


def check_theme_focus(root: Path, src_files: list[Path]) -> Finding:
    focus_hits = grep(src_files, root, r":focus|Focus")
    target_hits = grep(src_files, root, r"min-(?:width|height)\s*:|setMinimum(?:Width|Height|Size)|\bpadding\s*:")
    evidence = [
        f"focus style/signal hits: {len(focus_hits)}",
        f"target sizing/padding hits: {len(target_hits)}",
        *cap(focus_hits[:8] + target_hits[:8], 16),
    ]
    if focus_hits and target_hits:
        return Finding("pass", "focus_targets", "Focus and target-size static signals are present.", evidence)
    return Finding("warn", "focus_targets", "Focus or target-size signals need manual verification.", evidence)


def check_performance(root: Path, src_files: list[Path], doc_files: list[Path]) -> Finding:
    perf_hits = grep(src_files, root, r"QElapsedTimer|performance_profile|performance_metrics|benchmark")
    test_hits = grep(src_files, root, r"test_performance|performance.*test")
    unsafe_trace_hits = unsafe_performance_trace_hits(src_files, root)
    benchmark_doc_hits = grep(
        doc_files,
        root,
        r"Benchmark Result|Performance And Benchmark Checks|benchmark report|benchmark corpus|UX_PERFORMANCE_BASELINE|Local Baseline|Timing method",
    )
    evidence = [
        f"performance instrumentation hits: {len(perf_hits)}",
        f"performance test hits: {len(test_hits)}",
        f"unsafe trace detail hits: {len(unsafe_trace_hits)}",
        f"benchmark evidence doc hits: {len(benchmark_doc_hits)}",
        *cap(unsafe_trace_hits[:8] + perf_hits[:8] + test_hits[:8] + benchmark_doc_hits[:8], 24),
    ]
    if unsafe_trace_hits:
        return Finding(
            "fail",
            "performance",
            "Performance trace detail may expose private file/path metadata.",
            evidence,
            required=True,
        )
    if perf_hits and benchmark_doc_hits:
        return Finding("pass", "performance", "Performance timing/profile signals and benchmark evidence templates are present.", evidence)
    if perf_hits:
        return Finding("warn", "performance", "Performance timing/profile signals are present, but benchmark evidence documentation is missing.", evidence)
    return Finding("warn", "performance", "No performance timing/profile signals found.", evidence)


def check_telemetry(root: Path, files: list[Path]) -> Finding:
    telemetry_hits = grep(files, root, r"telemetry|analytics|event inventory|modal_shown|archive_open_completed")
    evidence = [f"telemetry/event documentation hits: {len(telemetry_hits)}", *cap(telemetry_hits, 12)]
    if telemetry_hits:
        return Finding("pass", "telemetry", "Telemetry/event inventory documentation is present.", evidence)
    return Finding("warn", "telemetry", "No telemetry or event-inventory signal found.", evidence)


def check_validation_docs(root: Path, doc_files: list[Path]) -> Finding:
    required_terms = [
        "Keyboard QA",
        "Pointer",
        "Contrast",
        "Layout",
        "Status",
        "Internationalization",
        "Performance",
        "Consistency",
        "Telemetry",
        "A/B",
        "Release-Hardening",
    ]
    text = "\n".join(read_file(path) for path in doc_files)
    missing = [term for term in required_terms if term.lower() not in text.lower()]
    evidence = [f"covered terms: {len(required_terms) - len(missing)}/{len(required_terms)}"]
    if missing:
        evidence.extend(f"missing term: {term}" for term in missing)
        return Finding("fail", "validation_docs", "Validation docs are missing required acceptance areas.", evidence, required=True)
    return Finding("pass", "validation_docs", "Validation docs cover required roadmap acceptance areas.", evidence, required=True)


def check_completion_ledger(root: Path) -> Finding:
    path = root / "docs" / "ROADMAP_COMPLETION.md"
    if not path.exists():
        return Finding(
            "fail",
            "completion_ledger",
            "Roadmap completion ledger is missing.",
            ["missing: docs/ROADMAP_COMPLETION.md"],
            required=True,
        )

    text = read_file(path)
    placeholder_hits: list[str] = []
    placeholder_re = re.compile(r"\|\s*(?:Pending|TBD|TODO)\s*\|", re.IGNORECASE)
    for number, line in enumerate(text.splitlines(), start=1):
        if placeholder_re.search(line):
            placeholder_hits.append(f"docs/ROADMAP_COMPLETION.md:{number}: {line.strip()}")

    required_sections = [
        "Current Completion Checklist",
        "Release Revalidation Commands",
        "Definition Of Done For Roadmap Claims",
    ]
    missing_sections = [section for section in required_sections if section not in text]
    evidence = [
        f"placeholder status rows: {len(placeholder_hits)}",
        f"required sections present: {len(required_sections) - len(missing_sections)}/{len(required_sections)}",
        *cap(placeholder_hits + [f"missing section: {section}" for section in missing_sections], 12),
    ]
    if placeholder_hits or missing_sections:
        return Finding(
            "fail",
            "completion_ledger",
            "Roadmap completion ledger still has unresolved placeholder statuses or missing sections.",
            evidence,
            required=True,
        )
    return Finding(
        "pass",
        "completion_ledger",
        "Roadmap completion ledger is evidence-backed and has no placeholder completion statuses.",
        evidence,
        required=True,
    )


def build_report(root: Path) -> list[Finding]:
    src_files = iter_text_files(root, ["src", "meson.build"])
    doc_files = iter_text_files(root, ["docs", "README.md"])
    script_files = iter_text_files(root, ["scripts"])
    all_files = src_files + doc_files + script_files
    return [
        *check_docs(root),
        check_validation_docs(root, doc_files),
        check_completion_ledger(root),
        check_audit_regression_fixtures(),
        check_model_view(root, src_files),
        check_i18n(root, src_files),
        check_accessibility(root, src_files),
        check_theme_focus(root, src_files),
        check_modality(root, src_files),
        check_performance(root, src_files, doc_files),
        check_telemetry(root, all_files),
    ]


def as_dict(finding: Finding) -> dict[str, object]:
    return {
        "status": finding.status,
        "area": finding.area,
        "message": finding.message,
        "required": finding.required,
        "evidence": finding.evidence,
    }


def print_markdown(findings: list[Finding]) -> None:
    print("# PakFu UX Roadmap Static Audit")
    print()
    for finding in findings:
        label = finding.status.upper()
        required = " required" if finding.required else ""
        print(f"## {label}: {finding.area}{required}")
        print(finding.message)
        for item in finding.evidence:
            print(f"- {item}")
        print()


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")

    parser = argparse.ArgumentParser(
        description="Static, read-only audit for PakFu UX roadmap completion signals.",
    )
    parser.add_argument(
        "--root",
        default=Path(__file__).resolve().parents[1],
        type=Path,
        help="Repository root to audit.",
    )
    parser.add_argument(
        "--format",
        choices=("markdown", "json"),
        default="markdown",
        help="Output format.",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Return non-zero when required checks fail or any warning remains.",
    )
    args = parser.parse_args()

    root = args.root.resolve()
    findings = build_report(root)

    if args.format == "json":
        print(json.dumps([as_dict(finding) for finding in findings], indent=2))
    else:
        print_markdown(findings)

    has_required_fail = any(f.status == "fail" and f.required for f in findings)
    has_warning = any(f.status == "warn" for f in findings)
    if has_required_fail or (args.strict and has_warning):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
