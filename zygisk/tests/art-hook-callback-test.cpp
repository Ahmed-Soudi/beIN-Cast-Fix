#include "instrumentation.hpp"
#include "jit_code_cache.hpp"

#include <cassert>
#include <cstdio>

using namespace lsplant;
using namespace lsplant::art;
namespace {
const auto update_symbol = "_ZN3art15instrumentation15Instrumentation21UpdateMethodsCodeImplEPNS_9ArtMethodEPKv";
const auto move_symbol = "_ZN3art3jit12JitCodeCache18MoveObsoleteMethodEPNS_9ArtMethodES3_";
const auto collect_symbol = "_ZN3art3jit12JitCodeCache12DoCollectionEPNS_6ThreadE";
const auto old_collect_symbol = "_ZN3art3jit12JitCodeCache19GarbageCollectCacheEPNS_6ThreadE";
ArtMethod *last_updated = nullptr;
int update_calls = 0;
std::vector<int> sequence;

void original_update(void *, ArtMethod *method, const void *code) {
    last_updated = method;
    method->entry = const_cast<void *>(code);
    ++update_calls;
}
void original_move(void *, ArtMethod *old_method, ArtMethod *new_method) {
    sequence.push_back(1);
    new_method->data = old_method->data;
    old_method->data = nullptr;
}
void original_collection(void *, Thread *) { sequence.push_back(2); }
}  // namespace

int main() {
    using Update = void (*)(void *, ArtMethod *, const void *);
    using Collection = void (*)(void *, Thread *);
    alignas(std::max_align_t) unsigned char context[256]{};
    InitInfo handler;
    int installations = 0;
    Update update = nullptr;
    handler.art_symbol_resolver = [](std::string_view name) -> void * {
        return name == update_symbol ? reinterpret_cast<void *>(original_update) : nullptr;
    };
    handler.inline_hooker = [&](void *original, void *replacement) -> void * {
        ++installations;
        update = reinterpret_cast<Update>(replacement);
        return original;
    };
    JNIEnv env;
    test_debuggable = false;
    test_sdk = 33;
    assert(Instrumentation::Init(&env, handler));
    assert(installations == 1 && update);

    ArtMethod target, backup, plain;
    int trampoline{}, compiled{}, existing{};
    target.entry = &trampoline;
    backup.entry = &existing;
    hook_target = &target;
    hook_backup = &backup;
    update(context, &target, &compiled);
    assert(update_calls == 1 && last_updated == &backup);
    assert(target.entry == &trampoline && backup.entry == &compiled);

    // A requested entrypoint that already matches the target is propagated
    // normally, following the pinned helper's original condition.
    update(context, &target, &trampoline);
    assert(update_calls == 2 && last_updated == &target);
    update(context, &plain, &compiled);
    assert(update_calls == 3 && last_updated == &plain && plain.entry == &compiled);
    deoptimized = &target;
    update(context, &target, &compiled);
    assert(update_calls == 3 && target.entry == &trampoline);
    deoptimized = nullptr;

    handler.art_symbol_resolver = [](std::string_view) -> void * { return nullptr; };
    assert(!Instrumentation::Init(&env, handler));
    handler.art_symbol_resolver = [](std::string_view name) -> void * {
        return name == update_symbol ? reinterpret_cast<void *>(original_update) : nullptr;
    };
    handler.inline_hooker = [](void *, void *) -> void * { return nullptr; };
    assert(!Instrumentation::Init(&env, handler));
    test_sdk = 32;
    assert(Instrumentation::Init(&env, handler));

    Collection collection = nullptr;
    test_sdk = 33;
    handler.art_symbol_resolver = [](std::string_view name) -> void * {
        if (name == move_symbol) return reinterpret_cast<void *>(original_move);
        if (name == collect_symbol) return reinterpret_cast<void *>(original_collection);
        return nullptr;
    };
    handler.inline_hooker = [&](void *original, void *replacement) -> void * {
        collection = reinterpret_cast<Collection>(replacement);
        return original;
    };
    assert(jit::JitCodeCache::Init(handler) && collection);
    int jit_data{};
    target.data = &jit_data;
    backup.data = nullptr;
    movements = {{&target, &backup}};
    sequence.clear();
    collection(context, nullptr);
    assert((sequence == std::vector<int>{1, 2}));
    assert(target.data == nullptr && backup.data == &jit_data);

    // The old collection entrypoint retains the same migration ordering.
    handler.art_symbol_resolver = [](std::string_view name) -> void * {
        if (name == move_symbol) return reinterpret_cast<void *>(original_move);
        if (name == old_collect_symbol) return reinterpret_cast<void *>(original_collection);
        return nullptr;
    };
    assert(jit::JitCodeCache::Init(handler));
    target.data = &jit_data;
    sequence.clear();
    collection(context, nullptr);
    assert((sequence == std::vector<int>{1, 2}));

    handler.art_symbol_resolver = [](std::string_view name) -> void * {
        return name == move_symbol ? reinterpret_cast<void *>(original_move) : nullptr;
    };
    assert(!jit::JitCodeCache::Init(handler));
    handler.art_symbol_resolver = [](std::string_view name) -> void * {
        if (name == move_symbol) return reinterpret_cast<void *>(original_move);
        if (name == collect_symbol) return reinterpret_cast<void *>(original_collection);
        return nullptr;
    };
    handler.inline_hooker = [](void *, void *) -> void * { return nullptr; };
    assert(!jit::JitCodeCache::Init(handler));
    puts("LSPlant actual-header release update and JIT migration callback scenarios passed");
}
