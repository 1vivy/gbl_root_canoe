#!/usr/bin/env python3
"""Stage a version-matched KSU build without accepting hosted or legacy assets."""
import json
import shutil
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
version = sys.argv[3]
if not (source / "index.html").is_file():
    raise SystemExit("KSU dist is missing index.html; build the application KSU target first")
manifest = json.loads((source / "manifest.json").read_text())
expected = {"product": "canoe-boot-manager", "version": version, "runtime": "ksu"}
if any(manifest.get(key) != value for key, value in expected.items()):
    raise SystemExit(f"KSU dist manifest must declare {expected}")
if any(path.is_symlink() for path in source.rglob("*")):
    raise SystemExit("KSU dist must be self-contained (no symlinks)")
if destination.exists():
    shutil.rmtree(destination)
shutil.copytree(source, destination)
