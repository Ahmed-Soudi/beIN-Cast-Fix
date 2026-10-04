#!/usr/bin/env python3
"""Validate and package the root module; no app APK is shipped or modified."""
import hashlib
from pathlib import Path
import struct
import sys
import zipfile

stage, output = map(Path, sys.argv[1:])
required = [
    'module.prop', 'customize.sh', 'skip_mount', 'diagnostic_mode', 'hook.dex',
    'META-INF/com/google/android/update-binary',
    'META-INF/com/google/android/updater-script',
    'zygisk/arm64-v8a.so', 'zygisk/armeabi-v7a.so',
    'README.md', 'THIRD_PARTY.md', 'LICENSE',
    'patches/README.md', 'patches/apply-lsplant-compat.py',
    'patches/lsplant-compat.patch', 'patches/lsplant-android13-init.json',
    'native/ART-PROFILE.md', 'native/ArtCompatibility.hpp', 'native/DiagnosticMode.hpp', 'native/DEPENDENCIES.md',
]
for item in required:
    if not (stage / item).is_file():
        raise SystemExit('Missing module payload: ' + item)
if (stage / 'diagnostic_mode').read_bytes() != b'engine\n':
    raise SystemExit('Diagnostic package must default to engine-only mode')
for abi, expected_class, expected_machine in [('arm64-v8a', 2, 183), ('armeabi-v7a', 1, 40)]:
    library = (stage / 'zygisk' / (abi + '.so')).read_bytes()
    if library[:4] != b'\x7fELF' or library[4] != expected_class or library[5] != 1:
        raise SystemExit('Invalid ELF format for ' + abi)
    if struct.unpack_from('<H', library, 18)[0] != expected_machine:
        raise SystemExit('Wrong ELF architecture for ' + abi)
dex = (stage / 'hook.dex').read_bytes()
if not dex.startswith(b'dex\n'):
    raise SystemExit('Hook helper is not a DEX file')
if hashlib.sha1(dex[32:]).digest() != dex[12:32]:
    raise SystemExit('Hook helper DEX signature mismatch')
if any(stage.rglob('*.apk')):
    raise SystemExit('Root module must not contain an APK')

files = sorted(p for p in stage.rglob('*') if p.is_file())
manifest = ''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(stage).as_posix() + '\n' for p in files)
(stage / 'SHA256SUMS').write_text(manifest)
output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for path in sorted(p for p in stage.rglob('*') if p.is_file()):
        name = path.relative_to(stage).as_posix()
        info = zipfile.ZipInfo(name, (2026, 10, 3, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = (0o100755 if name.endswith('.sh') or name.endswith('/update-binary') else 0o100644) << 16
        archive.writestr(info, path.read_bytes())
print(output.name + ': ' + hashlib.sha256(output.read_bytes()).hexdigest())
