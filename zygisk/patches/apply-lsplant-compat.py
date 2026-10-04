#!/usr/bin/env python3
"""Apply the local compatibility backport only to exact pinned LSPlant files."""
import hashlib
import json
from pathlib import Path
import sys


def apply(root):
    manifest = json.loads(Path(__file__).with_name("lsplant-android13-init.json").read_text())
    pending = []
    for entry in manifest["files"]:
        path = root / entry["path"]
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest == entry["after_sha256"]:
            print("LSPlant compatibility already applied:", entry["path"])
            continue
        if digest != entry["before_sha256"]:
            raise SystemExit("Refusing unsupported LSPlant source hash: " + entry["path"] + " " + digest)
        text = data.decode("utf-8")
        for replacement in entry["replacements"]:
            if text.count(replacement["before"]) != 1:
                raise SystemExit("Expected patch context is not unique: " + entry["path"])
            text = text.replace(replacement["before"], replacement["after"], 1)
        result = text.encode("utf-8")
        if hashlib.sha256(result).hexdigest() != entry["after_sha256"]:
            raise SystemExit("Patched LSPlant source hash mismatch: " + entry["path"])
        pending.append((path, result))
    # Validate every source before changing any, so mismatched revisions cannot
    # leave a partially applied compatibility patch.
    for path, result in pending:
        path.write_bytes(result)
        print("Applied checked LSPlant compatibility:", path.relative_to(root))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: apply-lsplant-compat.py /path/to/pinned/lsplant")
    apply(Path(sys.argv[1]))
