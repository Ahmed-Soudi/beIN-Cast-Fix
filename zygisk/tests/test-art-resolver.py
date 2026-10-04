"""Exercise the native production ELF/XZ parser with generated ARM64 fixtures."""
import lzma
from pathlib import Path
import struct
import subprocess
import sys

EHDR = struct.Struct("<16sHHIQQQIHHHHHH")
SHDR = struct.Struct("<IIQQQQIIQQ")
SYM = struct.Struct("<IBBHQQ")
SHORTY = "_ZN3artL15GetMethodShortyEP7_JNIEnvP10_jmethodID"
EXACT_SHORTY = "_ZN3art15GetMethodShortyEP7_JNIEnvP10_jmethodID"


def elf(sections):
    names = b"\0"
    offsets = []
    for name, *_ in sections:
        offsets.append(len(names))
        names += name.encode() + b"\0"
    # Section one is the section-name string table in every fixture.
    sections[0] = (".shstrtab", 3, names, 0, 0)
    data = bytearray(EHDR.size)
    headers = [(0,) * 10]
    for name_offset, (_name, kind, payload, link, entry_size) in zip(offsets, sections):
        offset = len(data)
        data.extend(payload)
        headers.append((name_offset, kind, 0, 0, offset, len(payload), link, 0, 1, entry_size))
    shoff = len(data)
    data.extend(b"".join(SHDR.pack(*header) for header in headers))
    ident = b"\x7fELF\x02\x01\x01" + bytes(9)
    data[:EHDR.size] = EHDR.pack(ident, 3, 183, 1, 0, 0, shoff, 0, EHDR.size, 0, 0,
                                SHDR.size, len(headers), 1)
    return bytes(data)


def symbol_data(symbols):
    strings = b"\0"
    entries = bytes(SYM.size)
    for name, value, size, section in symbols:
        index = len(strings)
        strings += name.encode() + b"\0"
        entries += SYM.pack(index, 2, 0, section, value, size)
    return strings, entries


def mini_elf():
    strings, symbols = symbol_data([
        (SHORTY + ".__uniq.123456", 0x1234, 16, 1),
        (EXACT_SHORTY, 0x2340, 16, 1),
        ("Exported", 0x3000, 16, 1),
        ("Outside", 0x9000, 16, 1),
        ("AddressOverflow", (1 << 64) - 1, 16, 1),
        ("SizeOverflow", 0x1100, (1 << 64) - 1, 1),
        ("Undefined", 0x1100, 16, 0),
        ("Absolute", 0x1100, 16, 0xfff1),
        ("BadSection", 0x1100, 16, 99),
        ("ExtendedSection", 0x1100, 16, 0xffff),
    ])
    # Malformed symbol string offsets are rejected by each lookup.
    symbols += SYM.pack(0xffffffff, 2, 0, 1, 0x1100, 16)
    return elf([(".shstrtab", 3, b"", 0, 0), (".strtab", 3, strings, 0, 0),
                (".symtab", 2, symbols, 2, SYM.size)])


def outer(debug=None):
    strings, symbols = symbol_data([("Exported", 0x2100, 16, 1)])
    sections = [(".shstrtab", 3, b"", 0, 0), (".dynstr", 3, strings, 0, 0),
                (".dynsym", 11, symbols, 2, SYM.size)]
    if debug is not None:
        sections.append((".gnu_debugdata", 1, debug, 0, 0))
    return elf(sections)


def patch_header(data, field, value):
    values = list(EHDR.unpack(data[:EHDR.size]))
    values[field] = value
    return EHDR.pack(*values) + data[EHDR.size:]


def patch_section(data, section_index, field, value):
    offset = EHDR.unpack(data[:EHDR.size])[6] + section_index * SHDR.size
    values = list(SHDR.unpack(data[offset:offset + SHDR.size]))
    values[field] = value
    return data[:offset] + SHDR.pack(*values) + data[offset + SHDR.size:]


def run():
    binary, directory = sys.argv[1], Path(sys.argv[2])
    directory.mkdir(parents=True, exist_ok=True)
    inner = mini_elf()
    cases = [("stripped", outer(), "stripped")]
    for name, check in [("crc32", lzma.CHECK_CRC32), ("crc64", lzma.CHECK_CRC64),
                        ("sha256", lzma.CHECK_SHA256)]:
        cases.append((name, outer(lzma.compress(inner, check=check)), "debug"))
    for name, filter_id in [("x86", lzma.FILTER_X86), ("arm", lzma.FILTER_ARM),
                            ("armthumb", lzma.FILTER_ARMTHUMB)]:
        compressed = lzma.compress(inner, filters=[{"id": filter_id}, {"id": lzma.FILTER_LZMA2}])
        cases.append(("bcj-" + name, outer(compressed), "debug"))
    compressed = lzma.compress(inner)
    # Successful decoding past the initial 1 MiB allocation exercises the
    # XZ_SINGLE full-decode retry, rather than only its rejection boundary.
    cases.append(("xz-buffer-growth", outer(lzma.compress(inner + bytes(1024 * 1024))), "debug"))
    corrupted = bytearray(compressed)
    corrupted[-12] ^= 1
    valid = outer(compressed)
    cases += [
        ("truncated-xz", outer(compressed[:-8]), "reject"),
        ("corrupt-xz", outer(bytes(corrupted)), "reject"),
        ("xz-trailing", outer(compressed + b"garbage"), "reject"),
        ("xz-concatenated", outer(compressed + compressed), "reject"),
        ("truncated-mini", outer(lzma.compress(inner[:40])), "reject"),
        ("wrong-mini-machine", outer(lzma.compress(patch_header(inner, 2, 62))), "reject"),
        ("mini-section-overflow", outer(lzma.compress(patch_header(inner, 6, (1 << 64) - 1))), "reject"),
        ("mini-string-overflow", outer(lzma.compress(patch_section(inner, 2, 4, (1 << 64) - 1))), "reject"),
        ("mini-bad-symbol-link", outer(lzma.compress(patch_section(inner, 3, 6, 99))), "reject"),
        ("mini-bad-symbol-entrysize", outer(lzma.compress(patch_section(inner, 3, 9, 1))), "reject"),
        ("outer-truncated", valid[:50], "reject"),
        ("outer-wrong-machine", patch_header(valid, 2, 62), "reject"),
        ("outer-section-overflow", patch_header(valid, 6, (1 << 64) - 1), "reject"),
        ("outer-debug-offset-overflow", patch_section(valid, 4, 4, (1 << 64) - 1), "reject"),
        ("outer-debug-size-overflow", patch_section(valid, 4, 5, (1 << 64) - 1), "reject"),
        ("outer-name-offset-overflow", patch_section(valid, 4, 0, 0xffffffff), "reject"),
    ]
    # A highly compressible oversized output proves the 64 MiB allocation cap.
    oversized = lzma.compress(bytes(64 * 1024 * 1024 + 1), preset=0)
    cases.append(("xz-output-limit", outer(oversized), "reject"))
    for name, data, expected in cases:
        path = directory / (name + ".so")
        path.write_bytes(data)
        result = subprocess.run([binary, str(path), expected], capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(name + " failed:\n" + result.stderr)
        if expected == "debug" and "GetMethodShorty resolved from .gnu_debugdata/.symtab" not in result.stderr:
            raise SystemExit(name + " did not log the mini-ELF symbol source")
    print("ART resolver fixtures passed:", len(cases))


if __name__ == "__main__":
    run()
