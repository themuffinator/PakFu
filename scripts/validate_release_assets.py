#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path

from release_assets import SUPPORTED_PLATFORMS, normalize_arch, parse_asset_name


def collect_assets(dist_dir: Path):
    assets = []
    for path in sorted(dist_dir.iterdir()):
        if not path.is_file():
            continue
        parsed = parse_asset_name(path.name)
        if parsed:
            assets.append(parsed)
    return assets


def validate_assets(
    assets,
    version: str,
    platform: str | None,
    arch: str | None,
) -> tuple[bool, list[str]]:
    normalized_arch = normalize_arch(arch) if arch else None
    filtered = [asset for asset in assets if asset.version == version]
    if platform:
        filtered = [asset for asset in filtered if asset.platform == platform]
    if normalized_arch:
        filtered = [asset for asset in filtered if asset.arch == normalized_arch]

    if not filtered:
        scope = f"version={version}"
        if platform:
            scope += f", platform={platform}"
        if normalized_arch:
            scope += f", arch={normalized_arch}"
        return False, [f"No release assets found for {scope}."]

    by_platform_arch: dict[tuple[str, str], set[str]] = defaultdict(set)
    for asset in filtered:
        by_platform_arch[(asset.platform, asset.arch)].add(asset.kind)

    errors: list[str] = []
    if platform:
        for (asset_platform, asset_arch), kinds in by_platform_arch.items():
            missing = {"portable", "installer"} - kinds
            if missing:
                errors.append(
                    f"Missing {', '.join(sorted(missing))} package(s) for "
                    f"{asset_platform}/{asset_arch} @ {version}."
                )
    else:
        for expected_platform in SUPPORTED_PLATFORMS:
            platform_rows = [
                kinds
                for (asset_platform, _), kinds in by_platform_arch.items()
                if asset_platform == expected_platform
            ]
            if not platform_rows:
                errors.append(f"Missing all assets for platform {expected_platform} @ {version}.")
                continue
            if not any({"portable", "installer"}.issubset(kinds) for kinds in platform_rows):
                errors.append(
                    f"Platform {expected_platform} does not have both portable and installer assets @ {version}."
                )

    return not errors, errors


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate PakFu release asset completeness and naming.",
    )
    parser.add_argument("--dist", default="dist", help="Distribution directory to inspect.")
    parser.add_argument("--version", required=True, help="Expected version.")
    parser.add_argument(
        "--platform",
        choices=SUPPORTED_PLATFORMS,
        help="Optional platform scope.",
    )
    parser.add_argument(
        "--arch",
        help="Optional architecture scope (x64, arm64, etc).",
    )
    args = parser.parse_args()

    dist_dir = Path(args.dist).resolve()
    if not dist_dir.exists():
        print(f"Distribution directory not found: {dist_dir}", file=sys.stderr)
        return 1

    assets = collect_assets(dist_dir)
    ok, errors = validate_assets(
        assets=assets,
        version=args.version,
        platform=args.platform,
        arch=args.arch,
    )
    if not ok:
        for message in errors:
            print(message, file=sys.stderr)
        return 1

    print(f"Release assets validated for version {args.version}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
