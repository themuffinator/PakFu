#!/usr/bin/env python3
from __future__ import annotations

import plistlib
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def managed_extensions_from_cpp() -> set[str]:
    source = (ROOT / "src/platform/file_associations.cpp").read_text(encoding="utf-8")
    matches = re.findall(r'\{\s*"([a-z0-9]+)"\s*,\s*"[^"]+"\s*,\s*QColor', source)
    if not matches:
        raise RuntimeError("Unable to find association specs in file_associations.cpp")
    return set(matches)


def desktop_entries(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        out[key] = value
    return out


def semicolon_values(value: str) -> set[str]:
    return {part for part in value.split(";") if part}


def plist_extensions(path: Path) -> set[str]:
    with path.open("rb") as fh:
        data = plistlib.load(fh)
    out: set[str] = set()
    for doc_type in data.get("CFBundleDocumentTypes", []):
        for ext in doc_type.get("CFBundleTypeExtensions", []):
            if ext:
                out.add(str(ext).lower())
    return out


def mime_glob_extensions(path: Path) -> set[str]:
    tree = ET.parse(path)
    out: set[str] = set()
    for glob in tree.findall(".//{http://www.freedesktop.org/standards/shared-mime-info}glob"):
        pattern = glob.attrib.get("pattern", "")
        if pattern.startswith("*."):
            out.add(pattern[2:].lower())
    return out


def main() -> int:
    managed = managed_extensions_from_cpp()

    desktop_path = ROOT / "packaging/linux/io.github.themuffinator.PakFu.desktop"
    mime_path = ROOT / "packaging/linux/io.github.themuffinator.PakFu.mime.xml"
    metainfo_path = ROOT / "packaging/linux/io.github.themuffinator.PakFu.metainfo.xml"
    plist_path = ROOT / "packaging/macos/Info.plist.in"

    desktop = desktop_entries(desktop_path)
    if "%F" not in desktop.get("Exec", ""):
        return fail("Linux desktop file must pass dropped/opened files with %F.")
    if desktop.get("Icon") != "io.github.themuffinator.PakFu":
        return fail("Linux desktop icon id does not match installed icon name.")
    if desktop.get("StartupWMClass") != "PakFu":
        return fail("Linux desktop file is missing StartupWMClass=PakFu.")

    desktop_exts = semicolon_values(desktop.get("X-PakFu-Managed-Extensions", ""))
    if desktop_exts != managed:
        return fail(
            "Linux desktop managed extension list is out of sync. "
            f"Missing: {sorted(managed - desktop_exts)} Extra: {sorted(desktop_exts - managed)}"
        )

    desktop_mimes = semicolon_values(desktop.get("MimeType", ""))
    required_mimes = {
        "application/x-idtech-pak",
        "application/x-idtech-pk3",
        "application/x-idtech-pk4",
        "application/x-doom-wad",
        "application/x-quake-bsp",
        "text/x-idtech-map",
        "image/x-idtech-wal",
        "video/x-roq",
        "audio/x-idwav",
        "model/x-idtech-md3",
    }
    missing_mimes = required_mimes - desktop_mimes
    if missing_mimes:
        return fail(f"Linux desktop file is missing required MIME types: {sorted(missing_mimes)}")

    custom_mime_exts = mime_glob_extensions(mime_path)
    required_custom_exts = {
        "pak", "sin", "pk3", "pk4", "pkz", "resources", "wad", "wad2", "wad3",
        "bsp", "map", "proc", "pcx", "wal", "swl", "m8", "m32", "mip", "lmp", "ftx",
        "cin", "roq", "bik", "idwav", "mdl", "md2", "fm", "md3", "mdc", "md4",
        "mdr", "skb", "skd", "mdm", "glm", "iqm", "md5mesh", "tan", "lwo",
        "cfg", "shader", "menu", "def", "mtr", "fontdat", "spr", "sp2", "spr2", "bk", "os",
        "dm2", "aas", "qvm", "crc", "tag", "mdx", "mds", "skc", "ska",
    }
    missing_custom_exts = required_custom_exts - custom_mime_exts
    if missing_custom_exts:
        return fail(f"Linux MIME package is missing custom globs: {sorted(missing_custom_exts)}")

    ET.parse(metainfo_path)
    plist_exts = plist_extensions(plist_path)
    if plist_exts != managed:
        return fail(
            "macOS Info.plist document types are out of sync. "
            f"Missing: {sorted(managed - plist_exts)} Extra: {sorted(plist_exts - managed)}"
        )

    print(f"Shell integration metadata covers {len(managed)} managed extensions.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
