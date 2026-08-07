#!/usr/bin/env python3
"""Add the etaHEN plugin metadata header to a PS5 ELF."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--title-id", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    if not re.fullmatch(r"[A-Za-z]{4}\d{5}", args.title_id):
        parser.error("--title-id must contain four letters and five digits")
    if not re.fullmatch(r"\d\.\d{2}", args.version):
        parser.error("--version must use x.xx format")

    header = (
        f"etaHEN_PLUGIN\0{args.title_id}\0{args.version}\0".encode("ascii")
    )
    args.output.write_bytes(header + args.elf.read_bytes())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
