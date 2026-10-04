"""Check pinned patch/idempotence, then compile its real hook adapter body."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def run():
    root, build = map(Path, sys.argv[1:])
    project = Path(__file__).resolve().parent.parent
    manifest = json.loads((project / "patches/lsplant-android13-init.json").read_text())
    apply = project / "patches/apply-lsplant-compat.py"
    with tempfile.TemporaryDirectory() as temporary:
        copied = Path(temporary)
        for entry in manifest["files"]:
            data = (root / entry["path"]).read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            if digest == entry["after_sha256"]:
                text = data.decode()
                for replacement in reversed(entry["replacements"]):
                    assert text.count(replacement["after"]) == 1
                    text = text.replace(replacement["after"], replacement["before"], 1)
                data = text.encode()
            assert hashlib.sha256(data).hexdigest() == entry["before_sha256"], entry["path"]
            path = copied / entry["path"]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        first = subprocess.run([sys.executable, str(apply), str(copied)], capture_output=True, text=True)
        assert first.returncode == 0, first.stderr
        states = [(copied / entry["path"]).read_bytes() for entry in manifest["files"]]
        for state, entry in zip(states, manifest["files"]):
            assert hashlib.sha256(state).hexdigest() == entry["after_sha256"]
        second = subprocess.run([sys.executable, str(apply), str(copied)], capture_output=True, text=True)
        assert second.returncode == 0, second.stderr
        assert states == [(copied / entry["path"]).read_bytes() for entry in manifest["files"]]

        # Unknown source changes must be refused, with no changes to the other
        # source. This verifies the all-files preflight, not just a text match.
        changed = copied / manifest["files"][1]["path"]
        changed.write_bytes(changed.read_bytes() + b"// unrelated change\n")
        first_path = copied / manifest["files"][0]["path"]
        text = first_path.read_text()
        for replacement in reversed(manifest["files"][0]["replacements"]):
            text = text.replace(replacement["after"], replacement["before"], 1)
        first_path.write_text(text)
        before = [(copied / entry["path"]).read_bytes() for entry in manifest["files"]]
        rejected = subprocess.run([sys.executable, str(apply), str(copied)], capture_output=True, text=True)
        assert rejected.returncode != 0
        assert before == [(copied / entry["path"]).read_bytes() for entry in manifest["files"]]

    # Compile the actual patched adapter. Replace only its unrelated JNI/API
    # includes with explicit test interfaces; keep every adapter implementation.
    helper = states[1].decode()
    assert helper.count('#include "jni_helper.hpp"') == 1
    assert helper.count('#include "lsplant.hpp"') == 1
    helper = helper.replace('#include "jni_helper.hpp"', '#include "compat-test-stubs.hpp"')
    helper = helper.replace('#include "lsplant.hpp"', '')
    (build / "hook_helper.hpp").write_text(helper)
    (build / "compat-test-stubs.hpp").write_text('''#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#define ANDROID_LOG_ERROR 6
inline int __android_log_print(int, const char *, const char *, ...) { return 0; }
namespace lsplant {
struct InitInfo {
    std::function<void *(void *, void *)> inline_hooker;
    std::function<void *(std::string_view)> art_symbol_resolver;
    std::function<void *(std::string_view)> art_symbol_prefix_resolver;
};
template <typename, template <typename...> class> inline constexpr bool is_instance_v = false;
template <template <typename...> class T, typename... Args>
inline constexpr bool is_instance_v<T<Args...>, T> = true;
}
''')
    print("LSPlant checked patch application, idempotence, and revision rejection passed")


if __name__ == "__main__":
    run()
