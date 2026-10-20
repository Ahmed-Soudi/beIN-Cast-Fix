#pragma once

#include "DiagnosticMode.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace bein {

enum class HookId {
    Visibility, RegisterNative, UnregisterNative, ClassStatus, JitCollection, CodeUpdate
};

struct HookSpec {
    const char *name;
    const char *symbol;
    HookId id;
};

// These exact entrypoints belong to the inspected ART profile. The caller
// must resolve and verify all of them before configuring this policy.
inline constexpr std::array<HookSpec, 6> kHookSpecs{{
    {"visibility",
     "_ZN3art11ClassLinker26VisiblyInitializedCallback22MarkVisiblyInitializedEPNS_6ThreadE",
     HookId::Visibility},
    {"native registration",
     "_ZN3art11ClassLinker14RegisterNativeEPNS_6ThreadEPNS_9ArtMethodEPKv",
     HookId::RegisterNative},
    {"native unregistration",
     "_ZN3art11ClassLinker16UnregisterNativeEPNS_6ThreadEPNS_9ArtMethodE",
     HookId::UnregisterNative},
    {"class status",
     "_ZN3art6mirror5Class9SetStatusENS_6HandleIS1_EENS_11ClassStatusEPNS_6ThreadE",
     HookId::ClassStatus},
    {"JIT collection",
     "_ZN3art3jit12JitCodeCache12DoCollectionEPNS_6ThreadE",
     HookId::JitCollection},
    {"method code updates",
     "_ZN3art15instrumentation15Instrumentation21UpdateMethodsCodeImplEPNS_9ArtMethodEPKv",
     HookId::CodeUpdate},
}};

class NativeHookProbe {
public:
    NativeHookProbe() = default;
    NativeHookProbe(const NativeHookProbe &) = delete;
    NativeHookProbe &operator=(const NativeHookProbe &) = delete;

    static constexpr std::size_t expected_installed(DiagnosticMode mode) {
        switch (mode) {
            case DiagnosticMode::Loader:
            case DiagnosticMode::Prepare: return 0;
            case DiagnosticMode::Registration: return 2;
            case DiagnosticMode::All: return 6;
            case DiagnosticMode::Visibility:
            case DiagnosticMode::Status:
            case DiagnosticMode::Collection:
            case DiagnosticMode::Instrumentation: return 1;
        }
        return 0;
    }

    bool configure(DiagnosticMode mode, const std::array<void *, 6> &targets) {
        if (configuration_attempted_) return fail("configuration attempted more than once");
        configuration_attempted_ = true;
        if (failure_) return false;
        if (!is_probe_mode(mode)) return fail("invalid mode for native hook probe");
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (!targets[i]) return fail("null configured target");
            for (std::size_t j = 0; j < i; ++j)
                if (targets[i] == targets[j]) return fail("aliased configured targets");
        }
        mode_ = mode;
        for (std::size_t i = 0; i < entries_.size(); ++i) entries_[i].target = targets[i];
        configured_ = true;
        return true;
    }

    template <class Backend>
    void *hook(void *target, void *replacement, Backend &&backend) {
        if (failure_) return nullptr;
        if (!configured_) return reject("hook requested before configuration");
        if (!target || !replacement) return reject("null hook request");
        if (target == replacement) return reject("replacement aliases target");
        const std::size_t index = find(target);
        if (index == entries_.size()) return reject("unrecognized hook target");
        auto &entry = entries_[index];
        if (entry.requested) return reject("duplicate hook request");
        entry.requested = true;
        if (!selected(mode_, kHookSpecs[index].id)) {
            entry.bypassed = true;
            return target;
        }
        void *backup = std::forward<Backend>(backend)(target, replacement);
        if (!backup) return reject("backend returned null backup");
        if (backup == target || backup == replacement)
            return reject("backend returned aliased backup");
        entry.owned = true;
        return backup;
    }

    template <class BackendDestroy>
    bool unhook(void *target, BackendDestroy &&backend) {
        if (!configured_) return fail("unhook requested before configuration");
        if (!target) return fail("null unhook request");
        const std::size_t index = find(target);
        if (index == entries_.size()) return fail("unrecognized unhook target");
        auto &entry = entries_[index];
        if (!entry.owned) return fail("unhook target is not owned");
        // Owned hooks may still be cleaned up after another request failed.
        // Such cleanup never clears the original sticky failure.
        if (!std::forward<BackendDestroy>(backend)(target)) return fail("backend unhook failed");
        entry.owned = false;
        return true;
    }

    bool complete() const {
        if (!configured_ || failure_ || requested() != entries_.size() ||
            installed() != expected_installed(mode_) ||
            bypassed() != entries_.size() - expected_installed(mode_)) return false;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const bool active = selected(mode_, kHookSpecs[i].id);
            if (entries_[i].owned != active || entries_[i].bypassed == active) return false;
        }
        return true;
    }

    std::size_t requested() const {
        std::size_t count = 0;
        for (const auto &entry : entries_) count += entry.requested;
        return count;
    }
    // Installation statistics count currently owned hooks, so a successful
    // unhook removes ownership and makes an otherwise complete probe incomplete.
    std::size_t installed() const {
        std::size_t count = 0;
        for (const auto &entry : entries_) count += entry.owned;
        return count;
    }
    std::size_t bypassed() const {
        std::size_t count = 0;
        for (const auto &entry : entries_) count += entry.bypassed;
        return count;
    }
    const char *error() const { return failure_ ? failure_ : "none"; }

private:
    struct Entry {
        void *target = nullptr;
        bool requested = false;
        bool owned = false;
        bool bypassed = false;
    };
    std::array<Entry, 6> entries_{};
    DiagnosticMode mode_ = DiagnosticMode::Loader;
    bool configuration_attempted_ = false;
    bool configured_ = false;
    const char *failure_ = nullptr;

    static constexpr bool is_probe_mode(DiagnosticMode mode) {
        switch (mode) {
            case DiagnosticMode::Prepare:
            case DiagnosticMode::Visibility:
            case DiagnosticMode::Registration:
            case DiagnosticMode::Status:
            case DiagnosticMode::Collection:
            case DiagnosticMode::Instrumentation:
            case DiagnosticMode::All: return true;
            case DiagnosticMode::Loader: return false;
        }
        return false;
    }
    static constexpr bool selected(DiagnosticMode mode, HookId id) {
        switch (mode) {
            case DiagnosticMode::All: return true;
            case DiagnosticMode::Visibility: return id == HookId::Visibility;
            case DiagnosticMode::Registration:
                return id == HookId::RegisterNative || id == HookId::UnregisterNative;
            case DiagnosticMode::Status: return id == HookId::ClassStatus;
            case DiagnosticMode::Collection: return id == HookId::JitCollection;
            case DiagnosticMode::Instrumentation: return id == HookId::CodeUpdate;
            case DiagnosticMode::Prepare:
            case DiagnosticMode::Loader: return false;
        }
        return false;
    }
    std::size_t find(void *target) const {
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].target == target) return i;
        return entries_.size();
    }
    bool fail(const char *reason) {
        if (!failure_) failure_ = reason;
        return false;
    }
    void *reject(const char *reason) {
        fail(reason);
        return nullptr;
    }
};

inline constexpr std::size_t expected_installed(DiagnosticMode mode) {
    return NativeHookProbe::expected_installed(mode);
}

}  // namespace bein
