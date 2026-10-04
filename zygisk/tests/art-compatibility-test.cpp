#include "ArtCompatibility.hpp"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {
const std::vector<std::string> available = {
    "_ZN3artL15GetMethodShortyEP7_JNIEnvP10_jmethodID.__uniq.test",
    "_ZN3art6Thread14CurrentFromGdbEv",
    "_ZN3art11ClassLinker26VisiblyInitializedCallback22MarkVisiblyInitializedEPNS_6ThreadE",
    "_ZN3art11ClassLinker14RegisterNativeEPNS_6ThreadEPNS_9ArtMethodEPKv",
    "_ZN3art11ClassLinker16UnregisterNativeEPNS_6ThreadEPNS_9ArtMethodE",
    "art_quick_to_interpreter_bridge", "art_quick_generic_jni_trampoline",
    "_ZN3art6mirror5Class13GetDescriptorEPNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE",
    "_ZN3art6mirror5Class11GetClassDefEv",
    "_ZN3art6mirror5Class9SetStatusENS_6HandleIS1_EENS_11ClassStatusEPNS_6ThreadE",
    "_ZN3art16ScopedSuspendAllC2EPKcb", "_ZN3art16ScopedSuspendAllD2Ev",
    "_ZN3art2gc23ScopedGCCriticalSectionC2EPNS_6ThreadENS0_7GcCauseENS0_13CollectorTypeE",
    "_ZN3art2gc23ScopedGCCriticalSectionD2Ev",
    "_ZN3art3jit12JitCodeCache18MoveObsoleteMethodEPNS_9ArtMethodES3_",
    "_ZN3art3jit12JitCodeCache12DoCollectionEPNS_6ThreadE",
    "_ZN3art15instrumentation15Instrumentation21UpdateMethodsCodeImplEPNS_9ArtMethodEPKv",
    "_ZN3art7Runtime9instance_E",
    "_ZN3art7Runtime20SetRuntimeDebugStateENS0_17RuntimeDebugStateE"
};
struct FixtureResolver {
    std::set<std::string> symbols{available.begin(), available.end()};
    void *find(std::string_view name, bool prefix = false) const {
        for (const auto &symbol : symbols) {
            if (symbol == name || (prefix && symbol.starts_with(name)))
                return reinterpret_cast<void *>(uintptr_t{0x10000});
        }
        return nullptr;
    }
};
std::vector<std::string> messages;
void record_log(bool, const char *message) { messages.emplace_back(message); }
void print_log(bool error, const char *message) {
    fprintf(error ? stderr : stdout, "%s\n", message);
}

void check_private_image(const char *path) {
    std::ifstream input(path, std::ios::binary);
    assert(input.is_open());
    std::vector<unsigned char> image{std::istreambuf_iterator<char>(input), {}};
    Elf64_Ehdr header{};
    assert(image.size() >= sizeof(header));
    memcpy(&header, image.data(), sizeof(header));
    assert(header.e_machine == EM_AARCH64 && header.e_ident[EI_CLASS] == ELFCLASS64);
    assert(header.e_phentsize == sizeof(Elf64_Phdr));
    assert(header.e_phoff <= image.size());
    assert(static_cast<size_t>(header.e_phnum) * sizeof(Elf64_Phdr) <= image.size() - header.e_phoff);
    // These are address intervals only. No ARM image is mapped executable and
    // no resolved pointer is invoked by this host test.
    constexpr uintptr_t bias = 0x70000000;
    std::vector<ArtResolver::Segment> segments;
    for (size_t i = 0; i < header.e_phnum; ++i) {
        Elf64_Phdr ph{};
        memcpy(&ph, image.data() + header.e_phoff + i * sizeof(ph), sizeof(ph));
        if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
        assert(ph.p_vaddr <= UINTPTR_MAX - bias);
        assert(ph.p_memsz <= UINTPTR_MAX - bias - ph.p_vaddr);
        segments.emplace_back(bias + ph.p_vaddr, bias + ph.p_vaddr + ph.p_memsz);
    }
    ArtResolver resolver(print_log);
    assert(resolver.initialize(image.data(), image.size(), bias, segments, EM_AARCH64));
    assert(resolver.build_id() == bein::kArtBuildId);
    assert(bein::profile_matches(resolver.build_id(), sizeof(void *)));
    assert(bein::preflight_api33_release(resolver, print_log));
    puts("Private device ART required release symbol preflight passed; no ARM code executed");
}
}  // namespace

int main(int argc, char **argv) {
    assert(bein::profile_matches(bein::kArtBuildId, 8));
    assert(!bein::profile_matches(bein::kArtBuildId, 4));
    assert(!bein::profile_matches("", 8));
    assert(!bein::profile_matches("unknown", 8));
    FixtureResolver complete;
    assert(bein::preflight_api33_release(complete, record_log));
    assert(messages.size() == available.size() + 1);
    // Each currently selected release group must be required. This includes
    // each half of the interpreter fallback and the new release update hook.
    for (const auto &symbol : available) {
        FixtureResolver missing;
        missing.symbols.erase(symbol);
        assert(!bein::preflight_api33_release(missing));
    }
    FixtureResolver setter;
    setter.symbols.erase("art_quick_to_interpreter_bridge");
    setter.symbols.erase("art_quick_generic_jni_trampoline");
    setter.symbols.emplace("_ZNK3art11ClassLinker27SetEntryPointsToInterpreterEPNS_9ArtMethodE");
    assert(bein::preflight_api33_release(setter));
    FixtureResolver old_collection;
    old_collection.symbols.erase("_ZN3art3jit12JitCodeCache12DoCollectionEPNS_6ThreadE");
    old_collection.symbols.emplace("_ZN3art3jit12JitCodeCache19GarbageCollectCacheEPNS_6ThreadE");
    assert(bein::preflight_api33_release(old_collection));
    FixtureResolver guessed;
    guessed.symbols.erase("_ZN3art3jit12JitCodeCache12DoCollectionEPNS_6ThreadE");
    guessed.symbols.emplace("_ZN3art3jit12JitCodeCache12DoCollectionEPNS_6ThreadE.unverified");
    assert(!bein::preflight_api33_release(guessed));
    FixtureResolver empty;
    empty.symbols.clear();
    messages.clear();
    assert(!bein::preflight_api33_release(empty, record_log));
    assert(messages.size() == available.size() + 1);
    puts("ART profile and required release preflight scenarios passed");
    assert(argc == 1 || argc == 2);
    if (argc == 2) check_private_image(argv[1]);
}
