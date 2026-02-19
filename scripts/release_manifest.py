#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path

from release_assets import SUPPORTED_PLATFORMS, parse_asset_name


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate PakFu release-manifest.json.")
    parser.add_argument("--dist", default="dist", help="Directory containing packaged artifacts.")
    parser.add_argument("--version", required=True, help="Release version.")
    parser.add_argument(
        "--output",
        default="dist/release-manifest.json",
        help="Output manifest path.",
    )
    args = parser.parse_args()

    dist_dir = Path(args.dist).resolve()
    out_path = Path(args.output).resolve()
    if not dist_dir.exists():
        raise RuntimeError(f"Distribution directory not found: {dist_dir}")

    assets = []
    ignored = []
    for path in sorted(dist_dir.iterdir()):
        if not path.is_file():
            continue
        parsed = parse_asset_name(path.name)
        if not parsed or parsed.version != args.version:
            ignored.append(path.name)
            continue
        assets.append(
            {
                "name": path.name,
                "platform": parsed.platform,
                "arch": parsed.arch,
                "kind": parsed.kind,
                "extension": parsed.extension,
                "installable": parsed.installable,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )

    payload = {
        "product": "pakfu",
        "version": args.version,
        "generated_at_utc": dt.datetime.now(dt.UTC).isoformat(),
        "distribution_policy": {
            platform: {
                "preferred_kind": "installer",
                "fallback_kind": "portable",
            }
            for platform in SUPPORTED_PLATFORMS
        },
        "assets": assets,
        "ignored_files": ignored,
    }

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote manifest: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
