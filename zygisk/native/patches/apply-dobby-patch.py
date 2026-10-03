#!/usr/bin/env python3
"""Apply the recorded Android portability changes to the exact pinned Dobby tree."""
import hashlib
import pathlib
import subprocess
import sys

EXPECTED_BLOBS = {
    "source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.asm":
        "73491816168ac18500860606a0a04f8a248fb956",
    "cmake/compiler_and_linker.cmake":
        "4dc29bd0e99e54367bb0c457397cc06e2162c26a",
}
PATCHED_BLOBS = {
    "source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.asm":
        "4c852b0a4ae38d5ffa15e50726c51f5fb8c26043",
    "cmake/compiler_and_linker.cmake":
        "d5db2f9a8bfef8cc9b5cdf1f1c7a810402f81ecc",
}


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: apply-dobby-patch.py <pinned-Dobby-directory>")
    directory = pathlib.Path(sys.argv[1]).resolve()
    actual_blobs = {}
    for filename in EXPECTED_BLOBS:
        data = (directory / filename).read_bytes()
        actual_blobs[filename] = hashlib.sha1(
            b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
    if actual_blobs == PATCHED_BLOBS:
        print("Checked Dobby Android ELF patch is already applied")
        return
    if actual_blobs != EXPECTED_BLOBS:
        raise SystemExit("Dobby patch refused: source does not match the exact pinned or patched bytes")
    patch = pathlib.Path(__file__).with_name("dobby-android-elf.patch")
    command = ["git", "-C", str(directory), "apply"]
    subprocess.run(command + ["--check", str(patch)], check=True)
    subprocess.run(command + [str(patch)], check=True)
    for filename, expected in PATCHED_BLOBS.items():
        data = (directory / filename).read_bytes()
        actual = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
        if actual != expected:
            raise SystemExit(f"Dobby patch output mismatch: {filename}")
    print("Applied checked Dobby Android ELF assembly/build patch")


if __name__ == "__main__":
    main()
