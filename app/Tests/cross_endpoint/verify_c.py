"""Drive the compiled production-C fixture runner; persist actual process results."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--vectors-dir", type=Path, default=ROOT)
    args = parser.parse_args()
    manifest = json.loads((args.vectors_dir / "manifest.json").read_text(encoding="utf-8"))
    results = {}
    for name, metadata in manifest.items():
        data = (args.vectors_dir / name).read_bytes()
        if len(data) != metadata["length"] or hashlib.sha256(data).hexdigest() != metadata["sha256"]:
            raise SystemExit(f"Frozen vector integrity failure: {name}")
        kind = metadata["kind"]
        if kind == "oversized_header":
            expectation = "oversized_header"
        elif name == "default_v2.bin":
            expectation = "default"
        elif kind == "frame":
            expectation = "frame"
        elif name.startswith("unsupported_"):
            expectation = "schema"
        elif kind == "discrepancy" or not metadata["valid"]:
            expectation = "invalid"
        else:
            expectation = "valid"
        process = subprocess.run(
            [str(args.executable.resolve()), str(args.vectors_dir / name), expectation],
            text=True,
            capture_output=True,
            check=False,
        )
        results[name] = {
            "expectation": expectation,
            "exit_code": process.returncode,
            "stdout": process.stdout.strip(),
            "stderr": process.stderr.strip(),
        }
    serialized = json.dumps(results, indent=2) + "\n"
    if args.output:
        args.output.write_text(serialized, encoding="utf-8", newline="\n")
    print(serialized)
    if any(result["exit_code"] for result in results.values()):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
