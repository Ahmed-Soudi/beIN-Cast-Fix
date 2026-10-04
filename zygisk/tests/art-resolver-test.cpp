#include "ArtResolver.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

namespace {
constexpr uintptr_t kBias = 0x70000000;
constexpr const char *kShorty = "_ZN3artL15GetMethodShortyEP7_JNIEnvP10_jmethodID";
void logger(bool error, const char *message) {
    std::cerr << (error ? "ERROR " : "INFO ") << message << '\n';
}
bool address(ArtResolver &resolver, const char *name, bool prefix, uintptr_t expected) {
    auto actual = reinterpret_cast<uintptr_t>(resolver.find(name, prefix));
    if (actual == expected) return true;
    std::cerr << "lookup mismatch: " << name << " expected=" << expected << " actual=" << actual << '\n';
    return false;
}
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) return 2;
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), {});
    ArtResolver resolver(logger);
    bool ready = resolver.initialize(data.data(), data.size(), kBias,
        {{kBias + 0x1000, kBias + 0x4000}}, EM_AARCH64);
    const std::string mode(argv[2]);
    if (mode == "reject") {
        if (ready || resolver.find(kShorty, true)) return 1;
        return 0;
    }
    if (!ready) return 1;
    const std::string expected_build_id = argc == 4 ? argv[3] : "";
    if (resolver.build_id() != expected_build_id) {
        std::cerr << "build ID mismatch: expected=" << expected_build_id
                  << " actual=" << resolver.build_id() << '\n';
        return 1;
    }
    // A supplied local device ELF can verify identity without assuming that
    // its production symbols resemble the generated lookup fixtures.
    if (mode == "identity") return 0;
    if (!address(resolver, "Exported", false, kBias + 0x2100)) return 1;
    if (mode == "stripped") {
        return resolver.find(kShorty, true) == nullptr ? 0 : 1;
    }
    if (mode != "debug") return 2;
    if (!address(resolver, kShorty, true, kBias + 0x1234) ||
        !address(resolver, kShorty, false, 0) ||
        !address(resolver, "_ZN3art15GetMethodShortyEP7_JNIEnvP10_jmethodID", false, kBias + 0x2340))
        return 1;
    for (const char *name : {"Outside", "AddressOverflow", "SizeOverflow", "Undefined", "Absolute", "BadString", "BadSection", "ExtendedSection"}) {
        if (!address(resolver, name, false, 0)) return 1;
    }
    return 0;
}
