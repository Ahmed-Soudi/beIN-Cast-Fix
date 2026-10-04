#include "hook_helper.hpp"

#include <cassert>
#include <cstdio>

struct FunctionHook : lsplant::Hooker<int(int), lsplant::tstring<'f'>> {
    static int replace(int value) { return value + 90; }
};
struct MemberHook : lsplant::MemHooker<int(void *, int), lsplant::tstring<'m'>> {
    static int replace(void *, int value) { return value + 90; }
};
struct FallbackHook : lsplant::Hooker<int(int), lsplant::tstring<'v'>> {
    static int replace(int value) { return value + 90; }
};

int function_backup(int value) { return value + 3; }
int member_backup(void *context, int value) { return *static_cast<int *>(context) + value; }

int main() {
    lsplant::InitInfo handler;
    int calls = 0;
    handler.inline_hooker = [&](void *, void *) -> void * { ++calls; return nullptr; };
    FunctionHook function;
    MemberHook member;
    assert(!lsplant::HookSymNoHandle(handler, nullptr, function));
    assert(calls == 0);
    assert(!lsplant::HookSymNoHandle(handler, reinterpret_cast<void *>(function_backup), function));
    assert(calls == 1 && function.backup == nullptr);
    assert(!lsplant::HookSymNoHandle(handler, reinterpret_cast<void *>(member_backup), member));
    assert(calls == 2 && !member.backup);

    handler.inline_hooker = [&](void *original, void *) -> void * { ++calls; return original; };
    assert(lsplant::HookSymNoHandle(handler, reinterpret_cast<void *>(function_backup), function));
    assert(function.backup != nullptr && function.backup(7) == 10);
    assert(lsplant::HookSymNoHandle(handler, reinterpret_cast<void *>(member_backup), member));
    alignas(std::max_align_t) int context[8] = {4};
    assert(member.backup && member.backup(context, 6) == 10);

    // A resolved first candidate with a failed backend must allow the next
    // alternative to install, instead of reporting a false success.
    function.backup = nullptr;
    FallbackHook fallback;
    handler.art_symbol_resolver = [](std::string_view) -> void * {
        return reinterpret_cast<void *>(function_backup);
    };
    calls = 0;
    handler.inline_hooker = [&](void *original, void *) -> void * {
        return ++calls == 1 ? nullptr : original;
    };
    assert(lsplant::HookSyms(handler, function, fallback));
    assert(calls == 2 && function.backup == nullptr && fallback.backup(7) == 10);

    fallback.backup = nullptr;
    handler.inline_hooker = [](void *, void *) -> void * { return nullptr; };
    assert(!lsplant::HookSyms(handler, function, fallback));
    assert(function.backup == nullptr && fallback.backup == nullptr);
    std::puts("LSPlant hook adapter backup scenarios passed");
}
