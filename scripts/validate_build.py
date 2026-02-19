#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


def run_command(command: list[str]) -> tuple[int, str, str]:
    process = subprocess.run(
        command,
        text=True,
        capture_output=True,
        env=os.environ.copy(),
        check=False,
    )
    return process.returncode, process.stdout, process.stderr


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a built PakFu binary.")
    parser.add_argument("--binary", required=True, help="Path to pakfu executable.")
    parser.add_argument("--expected-version", required=True, help="Expected version string.")
    args = parser.parse_args()

    binary = Path(args.binary).resolve()
    if not binary.exists():
        print(f"Binary not found: {binary}", file=sys.stderr)
        return 1

    if os.name != "nt":
        binary.chmod(binary.stat().st_mode | 0o111)

    code, out, err = run_command([str(binary), "--cli", "--version"])
    if code != 0:
        print("CLI version check failed.", file=sys.stderr)
        print(out, file=sys.stderr)
        print(err, file=sys.stderr)
        return 1
    merged = f"{out}\n{err}"
    if args.expected_version not in merged:
        print(
            f"Expected version {args.expected_version} was not found in CLI output.",
            file=sys.stderr,
        )
        print(merged, file=sys.stderr)
        return 1

    code, out, err = run_command([str(binary), "--cli", "--help"])
    if code != 0:
        print("CLI help check failed.", file=sys.stderr)
        print(out, file=sys.stderr)
        print(err, file=sys.stderr)
        return 1

    print(f"Build validation passed for {binary.name} ({args.expected_version}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
