#pragma once

#include "ArtResolver.hpp"

#include <cstdio>
#include <initializer_list>
#include <string_view>

namespace bein {

// This profile identifies the private, inspected ARM64 ART image. Symbol
// preflight alone does not establish compatible structure layouts or hooks.
inline constexpr std::string_view kArtBuildId = "a47994c420371ffbd05d16fc9a15ac9f";
inline bool profile_matches(std::string_view build_id, size_t pointer_size) {
    return pointer_size == 8 && build_id == kArtBuildId;
}

namespace compatibility_detail {
struct Symbol {
    std::string_view name;
    bool allow_prefix = false;
};

template <class Resolver>
bool resolve_group(Resolver &resolver, ArtResolver::Logger logger, const char *group,
                   std::initializer_list<Symbol> alternatives) {
    for (const auto &symbol : alternatives) {
        const bool exact = resolver.find(symbol.name, false) != nullptr;
        if (exact || (symbol.allow_prefix && resolver.find(symbol.name, true))) {
            if (logger) {
                char message[384];
                snprintf(message, sizeof(message), "ART preflight: %s resolved %.*s (%s)", group,
                         static_cast<int>(symbol.name.size()), symbol.name.data(),
                         exact ? "exact" : "allowed prefix");
                logger(false, message);
            }
            return true;
        }
    }
    if (logger) {
        char message[192];
        snprintf(message, sizeof(message), "ART preflight: required group missing: %s", group);
        logger(true, message);
    }
    return false;
}
}  // namespace compatibility_detail

// Matches the patched, pinned engine's mandatory API33 ARM release groups.
// JNI reflection and inline-hook installation can still fail later; this only
// prevents beginning native mutation with an unresolved required symbol group.
// The caller must first enforce the profile, API, architecture and non-debug
// runtime gates. Debug-only Instrumentation and optional Dex/JNI-ID helpers are
// deliberately outside this release preflight.
template <class Resolver>
bool preflight_api33_release(Resolver &resolver, ArtResolver::Logger logger = nullptr) {
    using compatibility_detail::resolve_group;
    bool ready = true;
    ready &= resolve_group(resolver, logger, "method shorty", {
        {"_ZN3artL15GetMethodShortyEP7_JNIEnvP10_jmethodID", true},
        {"_ZN3art15GetMethodShortyEP7_JNIEnvP10_jmethodID"}});
    ready &= resolve_group(resolver, logger, "current thread", {
        {"_ZN3art6Thread14CurrentFromGdbEv"}});
    ready &= resolve_group(resolver, logger, "class initialization", {
        {"_ZN3art11ClassLinker22FixupStaticTrampolinesEPNS_6ThreadENS_6ObjPtrINS_6mirror5ClassEEE"},
        {"_ZN3art11ClassLinker22FixupStaticTrampolinesENS_6ObjPtrINS_6mirror5ClassEEE"},
        {"_ZN3art11ClassLinker22FixupStaticTrampolinesEPNS_6mirror5ClassE"},
        {"_ZN3art11ClassLinker26VisiblyInitializedCallback29AdjustThreadVisibilityCounterEPNS_6ThreadEl"},
        {"_ZN3art11ClassLinker26VisiblyInitializedCallback22MarkVisiblyInitializedEPNS_6ThreadE"}});
    ready &= resolve_group(resolver, logger, "native registration", {
        {"_ZN3art11ClassLinker14RegisterNativeEPNS_6ThreadEPNS_9ArtMethodEPKv"},
        {"_ZN3art9ArtMethod14RegisterNativeEPKv"},
        {"_ZN3art9ArtMethod14RegisterNativeEPKvb"},
        {"_ZN3art6mirror9ArtMethod14RegisterNativeEPNS_6ThreadEPKvb"}});
    ready &= resolve_group(resolver, logger, "native unregistration", {
        {"_ZN3art11ClassLinker16UnregisterNativeEPNS_6ThreadEPNS_9ArtMethodE"},
        {"_ZN3art9ArtMethod16UnregisterNativeEv"},
        {"_ZN3art6mirror9ArtMethod16UnregisterNativeEPNS_6ThreadE"}});

    // The setter is one supported path; without it both bridge symbols are
    // required. A single bridge must never pass this compound requirement.
    if (resolver.find("_ZNK3art11ClassLinker27SetEntryPointsToInterpreterEPNS_9ArtMethodE", false)) {
        ready &= resolve_group(resolver, logger, "interpreter entrypoint setter", {
            {"_ZNK3art11ClassLinker27SetEntryPointsToInterpreterEPNS_9ArtMethodE"}});
    } else {
        ready &= resolve_group(resolver, logger, "interpreter bridge", {
            {"art_quick_to_interpreter_bridge"}});
        ready &= resolve_group(resolver, logger, "generic JNI trampoline", {
            {"art_quick_generic_jni_trampoline"}});
    }
    ready &= resolve_group(resolver, logger, "class descriptor", {
        {"_ZN3art6mirror5Class13GetDescriptorEPNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE"}});
    ready &= resolve_group(resolver, logger, "class definition", {
        {"_ZN3art6mirror5Class11GetClassDefEv"}});
    ready &= resolve_group(resolver, logger, "class status", {
        {"_ZN3art6mirror5Class9SetStatusENS_6HandleIS1_EENS_11ClassStatusEPNS_6ThreadE"},
        {"_ZN3art6mirror5Class9SetStatusENS_6HandleIS1_EENS1_6StatusEPNS_6ThreadE"}});
    ready &= resolve_group(resolver, logger, "suspend all", {
        {"_ZN3art16ScopedSuspendAllC2EPKcb"}, {"_ZN3art3Dbg9SuspendVMEv"}});
    ready &= resolve_group(resolver, logger, "resume all", {
        {"_ZN3art16ScopedSuspendAllD2Ev"}, {"_ZN3art3Dbg8ResumeVMEv"}});
    ready &= resolve_group(resolver, logger, "GC critical section enter", {
        {"_ZN3art2gc23ScopedGCCriticalSectionC2EPNS_6ThreadENS0_7GcCauseENS0_13CollectorTypeE"}});
    ready &= resolve_group(resolver, logger, "GC critical section exit", {
        {"_ZN3art2gc23ScopedGCCriticalSectionD2Ev"}});
    ready &= resolve_group(resolver, logger, "JIT obsolete method migration", {
        {"_ZN3art3jit12JitCodeCache18MoveObsoleteMethodEPNS_9ArtMethodES3_"}});
    ready &= resolve_group(resolver, logger, "JIT collection", {
        {"_ZN3art3jit12JitCodeCache19GarbageCollectCacheEPNS_6ThreadE"},
        {"_ZN3art3jit12JitCodeCache12DoCollectionEPNS_6ThreadE"}});
    ready &= resolve_group(resolver, logger, "release method code updates", {
        {"_ZN3art15instrumentation15Instrumentation21UpdateMethodsCodeImplEPNS_9ArtMethodEPKv"}});
    ready &= resolve_group(resolver, logger, "runtime instance", {
        {"_ZN3art7Runtime9instance_E"}});
    ready &= resolve_group(resolver, logger, "runtime debug state", {
        {"_ZN3art7Runtime17SetJavaDebuggableEb"},
        {"_ZN3art7Runtime20SetRuntimeDebugStateENS0_17RuntimeDebugStateE"}});
    if (logger) logger(!ready, ready ? "ART preflight: required release groups resolved" :
                                    "ART preflight: refusing native initialization");
    return ready;
}

}  // namespace bein
