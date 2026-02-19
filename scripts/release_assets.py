#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass

SUPPORTED_PLATFORMS = ("windows", "macos", "linux")
SUPPORTED_KINDS = ("portable", "installer")

EXPECTED_EXTENSIONS: dict[tuple[str, str], str] = {
    ("windows", "portable"): "zip",
    ("windows", "installer"): "msi",
    ("macos", "portable"): "zip",
    ("macos", "installer"): "pkg",
    ("linux", "portable"): "tar.gz",
    ("linux", "installer"): "AppImage",
}

ASSET_RE = re.compile(
    r"^pakfu-"
    r"(?P<version>\d+(?:\.\d+){2,})-"
    r"(?P<platform>windows|macos|linux)-"
    r"(?P<arch>[a-z0-9_]+)-"
    r"(?P<kind>portable|installer)\."
    r"(?P<ext>[a-z0-9.]+)$",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class ReleaseAsset:
    filename: str
    version: str
    platform: str
    arch: str
    kind: str
    extension: str
    installable: bool


def normalize_arch(raw_arch: str) -> str:
    value = raw_arch.strip().lower()
    if value in {"x86_64", "amd64", "x64"}:
        return "x64"
    if value in {"aarch64", "arm64"}:
        return "arm64"
    return value


def expected_extension(platform: str, kind: str) -> str:
    key = (platform.lower(), kind.lower())
    ext = EXPECTED_EXTENSIONS.get(key)
    if not ext:
        raise ValueError(f"Unsupported platform/kind pair: {platform}/{kind}")
    return ext


def is_installable(platform: str, kind: str, extension: str) -> bool:
    if kind.lower() != "installer":
        return False
    expected = expected_extension(platform, kind)
    return extension.lower() == expected.lower()


def expected_filename(version: str, platform: str, arch: str, kind: str) -> str:
    normalized_platform = platform.lower()
    normalized_arch = normalize_arch(arch)
    normalized_kind = kind.lower()
    ext = expected_extension(normalized_platform, normalized_kind)
    return f"pakfu-{version}-{normalized_platform}-{normalized_arch}-{normalized_kind}.{ext}"


def parse_asset_name(filename: str) -> ReleaseAsset | None:
    match = ASSET_RE.match(filename)
    if not match:
        return None
    version = match.group("version")
    platform = match.group("platform").lower()
    arch = normalize_arch(match.group("arch"))
    kind = match.group("kind").lower()
    extension = match.group("ext")
    expected = expected_extension(platform, kind)
    if extension.lower() != expected.lower():
        return None
    return ReleaseAsset(
        filename=filename,
        version=version,
        platform=platform,
        arch=arch,
        kind=kind,
        extension=extension,
        installable=is_installable(platform, kind, extension),
    )


def _main() -> int:
    parser = argparse.ArgumentParser(
        description="Resolve canonical PakFu release asset names.",
    )
    parser.add_argument("--version", required=True, help="Version string (numeric dot-segments).")
    parser.add_argument("--platform", required=True, choices=SUPPORTED_PLATFORMS)
    parser.add_argument("--arch", required=True, help="Architecture token (e.g. x64, arm64, x86_64).")
    parser.add_argument("--format", choices=["name", "json"], default="name")
    args = parser.parse_args()

    portable = expected_filename(args.version, args.platform, args.arch, "portable")
    installer = expected_filename(args.version, args.platform, args.arch, "installer")

    if args.format == "name":
        print(portable)
        print(installer)
        return 0

    payload = {
        "version": args.version,
        "platform": args.platform,
        "arch": normalize_arch(args.arch),
        "assets": [
            asdict(parse_asset_name(portable)),
            asdict(parse_asset_name(installer)),
        ],
    }
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
