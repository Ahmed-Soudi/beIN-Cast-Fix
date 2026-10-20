#include "NativeHookProbe.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <set>
#include <string_view>
#include <utility>

namespace {
using bein::DiagnosticMode;
using bein::NativeHookProbe;
std::array<int, 6> target_storage{}, replacement_storage{}, backup_storage{};
int unknown_storage{};

std::array<void *, 6> targets() {
    std::array<void *, 6> result{};
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = &target_storage[i];
    return result;
}
void *replacement(std::size_t index) { return &replacement_storage[index]; }
void *backup(std::size_t index) { return &backup_storage[index]; }
std::size_t target_index(void *target) {
    for (std::size_t i = 0; i < target_storage.size(); ++i)
        if (target == &target_storage[i]) return i;
    assert(false);
    return 0;
}
void expect_failure(const NativeHookProbe &probe) {
    assert(!probe.complete());
    assert(std::string_view(probe.error()) != "none");
}

void check_mode(DiagnosticMode mode, const std::set<std::size_t> &selected) {
    NativeHookProbe probe;
    assert(probe.configure(mode, targets()));
    assert(!probe.complete());
    assert(probe.requested() == 0 && probe.installed() == 0 && probe.bypassed() == 0);
    assert(NativeHookProbe::expected_installed(mode) == selected.size());
    assert(bein::expected_installed(mode) == selected.size());
    std::set<std::size_t> backend_targets;
    auto backend = [&](void *target, void *replace) -> void * {
        const auto index = target_index(target);
        assert(replace == replacement(index));
        assert(backend_targets.insert(index).second);
        return backup(index);
    };
    // The policy must use resolved target identity, independent of call order.
    constexpr std::array<std::size_t, 6> order{5, 3, 0, 4, 2, 1};
    std::size_t requested = 0;
    for (const auto index : order) {
        assert(!probe.complete());
        void *result = probe.hook(targets()[index], replacement(index), backend);
        assert(result == (selected.contains(index) ? backup(index) : targets()[index]));
        assert(probe.requested() == ++requested);
    }
    assert(probe.complete());
    assert(backend_targets == selected);
    assert(probe.installed() == selected.size());
    assert(probe.bypassed() == 6 - selected.size());
    assert(std::string_view(probe.error()) == "none");
}
}  // namespace

int main() {
    static_assert(bein::kHookSpecs.size() == 6);
    std::set<bein::HookId> ids;
    std::set<std::string_view> symbols;
    for (const auto &spec : bein::kHookSpecs) {
        assert(spec.name && *spec.name && spec.symbol && *spec.symbol);
        assert(ids.insert(spec.id).second);
        assert(symbols.insert(spec.symbol).second);
    }
    check_mode(DiagnosticMode::Prepare, {});
    check_mode(DiagnosticMode::Visibility, {0});
    check_mode(DiagnosticMode::Registration, {1, 2});
    check_mode(DiagnosticMode::Status, {3});
    check_mode(DiagnosticMode::Collection, {4});
    check_mode(DiagnosticMode::Instrumentation, {5});
    check_mode(DiagnosticMode::All, {0, 1, 2, 3, 4, 5});

    int calls = 0;
    auto backend = [&](void *target, void *) -> void * {
        ++calls;
        return backup(target_index(target));
    };
    auto destroy = [&](void *) { ++calls; return true; };
    {
        NativeHookProbe probe;
        assert(!probe.configure(DiagnosticMode::Loader, targets()));
        assert(!probe.configure(DiagnosticMode::Prepare, targets()));
        assert(!probe.hook(targets()[0], replacement(0), backend));
        assert(calls == 0 && probe.requested() == 0);
        expect_failure(probe);
    }
    {
        NativeHookProbe probe;
        assert(!probe.configure(static_cast<DiagnosticMode>(99), targets()));
        expect_failure(probe);
    }
    for (bool alias : {false, true}) {
        NativeHookProbe probe;
        auto invalid = targets();
        invalid[5] = alias ? invalid[0] : nullptr;
        assert(!probe.configure(DiagnosticMode::All, invalid));
        assert(!probe.configure(DiagnosticMode::All, targets()));
        assert(probe.requested() == 0 && probe.installed() == 0);
        expect_failure(probe);
    }
    {
        NativeHookProbe probe;
        assert(probe.configure(DiagnosticMode::All, targets()));
        assert(!probe.configure(DiagnosticMode::All, targets()));
        assert(!probe.hook(targets()[0], replacement(0), backend));
        assert(calls == 0 && probe.requested() == 0);
        expect_failure(probe);
    }
    {
        NativeHookProbe probe;
        assert(!probe.hook(targets()[0], replacement(0), backend));
        assert(!probe.configure(DiagnosticMode::All, targets()));
        assert(calls == 0);
        expect_failure(probe);
    }
    // Missing requests leave a healthy but incomplete probe; the final exact
    // request can finish it without reconfiguration or any duplicate request.
    {
        NativeHookProbe probe;
        assert(probe.configure(DiagnosticMode::All, targets()));
        for (std::size_t i = 0; i < 5; ++i)
            assert(probe.hook(targets()[i], replacement(i), backend) == backup(i));
        assert(probe.requested() == 5 && probe.installed() == 5);
        assert(!probe.complete() && std::string_view(probe.error()) == "none");
        assert(probe.hook(targets()[5], replacement(5), backend) == backup(5));
        assert(probe.complete());
    }
    calls = 0;
    // Duplicate bypass requests must fail without ever calling the backend.
    {
        NativeHookProbe probe;
        assert(probe.configure(DiagnosticMode::Prepare, targets()));
        assert(probe.hook(targets()[0], replacement(0), backend) == targets()[0]);
        assert(!probe.hook(targets()[0], replacement(0), backend));
        const std::string_view original_error(probe.error());
        assert(!probe.hook(targets()[1], replacement(1), backend));
        assert(!probe.configure(DiagnosticMode::All, targets()));
        assert(std::string_view(probe.error()) == original_error);
        assert(calls == 0 && probe.requested() == 1 && probe.bypassed() == 1);
        expect_failure(probe);
    }
    // A duplicate selected request must never invoke the backend twice.
    {
        NativeHookProbe probe;
        assert(probe.configure(DiagnosticMode::All, targets()));
        int attempts = 0;
        auto selected_backend = [&](void *, void *) -> void * { ++attempts; return backup(0); };
        assert(probe.hook(targets()[0], replacement(0), selected_backend) == backup(0));
        assert(!probe.hook(targets()[0], replacement(0), selected_backend));
        assert(!probe.hook(targets()[1], replacement(1), selected_backend));
        assert(attempts == 1 && probe.requested() == 1 && probe.installed() == 1);
        expect_failure(probe);
    }
    // Invalid requests are refused even in Prepare, before the bypass path.
    for (int invalid : {0, 1, 2, 3}) {
        NativeHookProbe probe;
        assert(probe.configure(DiagnosticMode::Prepare, targets()));
        void *target = invalid == 0 ? nullptr : targets()[0];
        void *replace = invalid == 1 ? nullptr : replacement(0);
        if (invalid == 2) target = &unknown_storage;
        if (invalid == 3) replace = target;
        assert(!probe.hook(target, replace, backend));
        assert(calls == 0 && probe.requested() == 0 && probe.bypassed() == 0);
        expect_failure(probe);
    }
    // Null, original-target and replacement aliases cannot count as a
    // successful installation, and a second request never retries the backend.
    for (int invalid : {0, 1, 2}) {
        NativeHookProbe probe;
        assert(probe.configure(DiagnosticMode::Visibility, targets()));
        int failures = 0;
        auto bad_backend = [&](void *target, void *replace) -> void * {
            ++failures;
            return invalid == 0 ? nullptr : invalid == 1 ? target : replace;
        };
        assert(!probe.hook(targets()[0], replacement(0), bad_backend));
        assert(!probe.hook(targets()[0], replacement(0), bad_backend));
        assert(!probe.hook(targets()[1], replacement(1), bad_backend));
        assert(failures == 1 && probe.requested() == 1 && probe.installed() == 0);
        expect_failure(probe);
    }
    // Unconfigured, unknown, unrequested, and bypassed targets never reach
    // BackendDestroy; only a valid successful install grants ownership.
    {
        NativeHookProbe probe;
        assert(!probe.unhook(targets()[0], destroy));
        assert(calls == 0);
        expect_failure(probe);
    }
    for (int invalid : {0, 1, 2}) {
        NativeHookProbe probe;
        assert(probe.configure(DiagnosticMode::All, targets()));
        void *target = invalid == 0 ? nullptr : invalid == 1 ? &unknown_storage : targets()[0];
        assert(!probe.unhook(target, destroy));
        assert(calls == 0);
        expect_failure(probe);
    }
    {
        NativeHookProbe probe;
        assert(probe.configure(DiagnosticMode::Visibility, targets()));
        assert(probe.hook(targets()[0], replacement(0), backend) == backup(0));
        assert(probe.hook(targets()[1], replacement(1), backend) == targets()[1]);
        assert(calls == 1);
        assert(!probe.unhook(targets()[1], destroy));
        assert(calls == 1 && probe.installed() == 1);
        const std::string_view original_error(probe.error());
        // A legitimate owned hook may be removed after the sticky failure,
        // without permitting another install or clearing the original error.
        assert(probe.unhook(targets()[0], destroy));
        assert(calls == 2 && probe.installed() == 0);
        assert(!probe.unhook(targets()[0], destroy));
        assert(calls == 2 && std::string_view(probe.error()) == original_error);
        expect_failure(probe);
    }
    {
        NativeHookProbe probe;
        assert(probe.configure(DiagnosticMode::All, targets()));
        for (std::size_t i = 0; i < 6; ++i)
            assert(probe.hook(targets()[i], replacement(i), backend) == backup(i));
        assert(probe.complete());
        int removals = 0;
        assert(!probe.unhook(targets()[2], [&](void *target) {
            assert(target == targets()[2]);
            ++removals;
            return false;
        }));
        assert(removals == 1 && probe.installed() == 6);
        const std::string_view original_error(probe.error());
        assert(probe.unhook(targets()[2], [&](void *target) {
            assert(target == targets()[2]);
            ++removals;
            return true;
        }));
        assert(removals == 2 && probe.installed() == 5);
        assert(std::string_view(probe.error()) == original_error);
        expect_failure(probe);
    }
    {
        NativeHookProbe probe;
        assert(probe.configure(DiagnosticMode::Registration, targets()));
        for (std::size_t i = 0; i < 6; ++i)
            assert(probe.hook(targets()[i], replacement(i), backend));
        assert(probe.complete());
        int removals = 0;
        assert(probe.unhook(targets()[1], [&](void *target) {
            assert(target == targets()[1]);
            ++removals;
            return true;
        }));
        assert(removals == 1 && probe.installed() == 1 && !probe.complete());
        assert(std::string_view(probe.error()) == "none");
        assert(!probe.unhook(backup(1), [&](void *) { ++removals; return true; }));
        assert(removals == 1);
        expect_failure(probe);
    }
    puts("Native hook probe mode, coverage, failure, and ownership scenarios passed");
}
