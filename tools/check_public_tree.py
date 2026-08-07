#!/usr/bin/env python3
"""Reject private/generated files from the public v1.0 source tree."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_ROOT_FILES = {
    "summary.json",
    "probe-events.jsonl",
    "probe.pid",
    "backups.json",
    "config.json",
    "tracker-state.bin",
    "tracker-state.prev.bin",
    "completed-state.bin",
    "diagnostics-state.bin",
}
FORBIDDEN_PARTS = {
    ".omx",
    "ps5-activity-backup",
    "ps5 backup",
    "archive",
    "builder/bin",
    "builder/obj",
    "release-build/dist",
}
# Loopback is intentional in the Media package and local tests. Reject only
# non-loopback private-network literals, which are likely to be console data.
PRIVATE_IP = re.compile(r"\b(?:10|192\.168)\.(?:\d{1,3}\.){1,2}\d{1,3}\b")


def main() -> int:
    failures: list[str] = []
    for path in ROOT.rglob("*"):
        relative = path.relative_to(ROOT)
        if not path.is_file():
            continue
        normalized = "/".join(relative.parts)
        if (
            relative.parts
            and relative.parts[0] == "release-assets"
        ) or normalized.startswith(
            ("release-build/dist/", "release-build/builder/bin/",
             "release-build/builder/obj/")
        ) or (relative.parts and relative.parts[0] == "activity-probe"
              and path.suffix.lower() in {".elf", ".plugin"}):
            continue
        if len(relative.parts) == 1 and relative.name in FORBIDDEN_ROOT_FILES:
            failures.append(f"private root file: {relative}")
        if any(part in normalized for part in FORBIDDEN_PARTS):
            failures.append(f"generated/private path: {relative}")
        if path.stat().st_size > 2_000_000 and relative.parts[0] != "dashboard":
            failures.append(f"unexpected large file: {relative}")
        if path.suffix.lower() in {".md", ".json", ".js", ".c", ".h", ".py"}:
            try:
                text = path.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                continue
            if PRIVATE_IP.search(text):
                failures.append(f"private IP literal: {relative}")

    required = [
        ROOT / "README.md",
        ROOT / "KNOWN_LIMITATIONS.md",
        ROOT / "activity-probe/main.c",
        ROOT / "dashboard/index.html",
        ROOT / "release-build/Makefile",
    ]
    failures.extend(f"missing required file: {path.relative_to(ROOT)}" for path in required if not path.exists())
    dashboard = ROOT / "dashboard/index.html"
    if dashboard.exists() and "1.38.0" in dashboard.read_text(encoding="utf-8"):
        failures.append("stale dashboard UI version 1.38.0")
    launcher = ROOT / "release-build/app/launcher/index.html"
    if launcher.exists() and "ACTV00003" in launcher.read_text(encoding="utf-8"):
        failures.append("stale launcher Title ID ACTV00003")
    if failures:
        print("Public-tree check failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print("Public-tree check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
