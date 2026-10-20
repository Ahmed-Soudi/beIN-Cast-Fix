#include <android/api-level.h>
#include <android/log.h>
#include <fcntl.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <string_view>

#include <dobby.h>
#include <lsplant.hpp>
#include <zygisk.hpp>

#include "ArtResolver.hpp"
#include "ArtCompatibility.hpp"
#include "DiagnosticMode.hpp"
#include "NativeHookProbe.hpp"

namespace {
constexpr char kLogTag[] = "BeINCastRoot";
constexpr char kProcess[] = "ptv.bein.mena";
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

// Operations are confined to the selected process. Specialization arguments,
// properties, mounts, identities, and application files are not changed.
// The standalone LSPlant engine does change ART state internally.
bool clear_exception(JNIEnv *env, const char *step) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear();
    LOGE("%s: JNI exception; skipping this setup step", step);
    return true;
}

class LocalFrame {
public:
    explicit LocalFrame(JNIEnv *env) : env_(env), active_(env->PushLocalFrame(64) == JNI_OK) {
        if (!active_) clear_exception(env_, "PushLocalFrame");
    }
    ~LocalFrame() { if (active_) env_->PopLocalFrame(nullptr); }
    explicit operator bool() const { return active_; }
private:
    JNIEnv *env_;
    bool active_;
};

void art_resolver_log(bool error, const char *message) {
    __android_log_print(error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, kLogTag, "%s", message);
}

ArtResolver g_art{art_resolver_log};
bein::NativeHookProbe g_probe;
std::array<void *, 6> g_targets{};

void *inline_hook(void *target, void *replacement) {
    const char *name = "<unknown>";
    for (size_t i = 0; i < g_targets.size(); ++i) {
        if (g_targets[i] == target) { name = bein::kHookSpecs[i].name; break; }
    }
    void *result = g_probe.hook(target, replacement, [](void *address, void *callback) -> void * {
        dobby_dummy_func_t original = nullptr;
        const int status = DobbyHook(address, reinterpret_cast<dobby_dummy_func_t>(callback), &original);
        if (status != 0 || !original) {
            LOGE("Dobby installation failed (status=%d); diagnostic invalid", status);
            return nullptr;
        }
        return reinterpret_cast<void *>(original);
    });
    if (!result) LOGE("native probe refused %s: %s", name, g_probe.error());
    else LOGI("native probe %s: %s", result == target ? "bypassed without patch" : "installed", name);
    return result;
}
bool inline_unhook(void *target) {
    return g_probe.unhook(target, [](void *address) { return DobbyDestroy(address) == 0; });
}

bool verify_release_runtime(JNIEnv *env) {
    jclass runtime_class = env->FindClass("dalvik/system/VMRuntime");
    if (clear_exception(env, "VMRuntime preflight class") || !runtime_class) return false;
    jmethodID get_runtime = env->GetStaticMethodID(runtime_class, "getRuntime", "()Ldalvik/system/VMRuntime;");
    if (clear_exception(env, "VMRuntime preflight getRuntime") || !get_runtime) return false;
    jobject runtime = env->CallStaticObjectMethod(runtime_class, get_runtime);
    if (clear_exception(env, "VMRuntime preflight instance") || !runtime) return false;
    jmethodID is_debuggable = env->GetMethodID(runtime_class, "isJavaDebuggable", "()Z");
    if (clear_exception(env, "VMRuntime preflight isJavaDebuggable") || !is_debuggable) return false;
    const jboolean debuggable = env->CallBooleanMethod(runtime, is_debuggable);
    if (clear_exception(env, "VMRuntime preflight debugging state")) return false;
    if (debuggable == JNI_TRUE) {
        LOGE("ART preflight: Java-debuggable runtime unsupported; skipped before hooks");
        return false;
    }
    LOGI("ART preflight: release Java runtime confirmed");
    return true;
}

bool initialize(JNIEnv *env, bein::DiagnosticMode mode) {
    LocalFrame frame(env);
    if (!frame || !g_art.open_loaded()) return false;
    // This profile's private class-status ABI was verified against one ARM64
    // runtime. OS API level alone does not identify an updatable ART build.
    if (!bein::profile_matches(g_art.build_id(), sizeof(void *))) {
        LOGE("ART preflight: unsupported build ID %s (%zu-bit); skipped before hooks",
             g_art.build_id().empty() ? "<absent>" : g_art.build_id().c_str(), sizeof(void *) * 8);
        return false;
    }
    if (!verify_release_runtime(env) ||
        !bein::preflight_api33_release(g_art, art_resolver_log)) {
        LOGE("ART preflight failed; skipped before native probe");
        return false;
    }
    if (mode == bein::DiagnosticMode::Loader) {
        LOGI("PROBE RESULT mode=loader valid=1 init=0 requested=0 installed=0 bypassed=0 expected=0");
        return true;
    }
    for (size_t i = 0; i < bein::kHookSpecs.size(); ++i) {
        g_targets[i] = g_art.find(bein::kHookSpecs[i].symbol);
    }
    if (!g_probe.configure(mode, g_targets)) {
        LOGE("native probe configuration rejected: %s", g_probe.error());
        return false;
    }
    LOGI("native probe mode=%s; expected real hooks=%zu; no callback DEX or Java hooks",
         bein::diagnostic_mode_name(mode), bein::NativeHookProbe::expected_installed(mode));
    LOGI("LSPlant internal hook messages include bypasses; only audited PROBE RESULT counts define this trial");
    lsplant::InitInfo info{
        .inline_hooker = inline_hook,
        .inline_unhooker = inline_unhook,
        .art_symbol_resolver = [](std::string_view name) { return g_art.find(name); },
        .art_symbol_prefix_resolver = [](std::string_view name) { return g_art.find(name, true); },
        .generated_class_name = "BeinCastRootHook_",
        .generated_source_name = "BeinCastRoot",
        .generated_field_name = "bridge",
        .generated_method_name = "{target}",
    };
    const bool initialized = lsplant::Init(env, info);
    const bool exception = clear_exception(env, "LSPlant.Init");
    const bool valid = initialized && !exception && g_probe.complete();
    LOGI("PROBE RESULT mode=%s valid=%d init=%d requested=%zu installed=%zu bypassed=%zu expected=%zu",
         bein::diagnostic_mode_name(mode), valid, initialized, g_probe.requested(),
         g_probe.installed(), g_probe.bypassed(), bein::NativeHookProbe::expected_installed(mode));
    if (!valid) LOGE("native probe invalid: %s; no retry", g_probe.error());
    // Bypassed callbacks have original targets as backups solely for this
    // diagnostic. No Hook/Deoptimize/MakeDexFileTrusted call is permitted.
    return valid;
}

class BeinCastRoot final : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override { api_ = api; env_ = env; }
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        if (!api_ || !env_ || !args || !args->nice_name) { unload(); return; }
        const char *name = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (!name) { clear_exception(env_, "read process name"); unload(); return; }
        selected_ = strcmp(name, kProcess) == 0;
        env_->ReleaseStringUTFChars(args->nice_name, name);
        if (!selected_) { unload(); return; }
        const int sdk = android_get_device_api_level();
        LOGI("selected %s (v9, API %d, %s)", kProcess, sdk, sizeof(void *) == 8 ? "64-bit" : "32-bit");
        if (sdk != 33 || sizeof(void *) != 8) {
            LOGE("native probe supports reviewed Android 13/API33 ARM64 only");
            selected_ = false;
            unload();
            return;
        }
        const int directory = api_->getModuleDir();
        bool read = directory >= 0 && read_mode(directory);
        if (directory >= 0) close(directory);
        if (!read) { selected_ = false; unload(); }
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!selected_) return;
        LOGI("app specialized; diagnostic mode=%s", bein::diagnostic_mode_name(mode_));
        const bool ready = initialize(env_, mode_);
        LOGI("diagnostic stage ready=%d, mode=%s; Cast override disabled; Java hooks unavailable", ready,
             bein::diagnostic_mode_name(mode_));
        // Keep the library resident for every selected diagnostic, even after
        // failure: partially installed native hooks may refer to this code.
    }
    void preServerSpecialize(zygisk::ServerSpecializeArgs *) override { unload(); }
private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool selected_ = false;
    bein::DiagnosticMode mode_ = bein::DiagnosticMode::Prepare;
    void unload() { if (api_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); }
    bool read_mode(int directory) {
        const int fd = openat(directory, "diagnostic_mode",
                              O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0) { LOGE("diagnostic mode file unavailable; skipping setup"); return false; }
        struct stat status{};
        if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
            status.st_size <= 0 || status.st_size > 16) {
            close(fd);
            LOGE("invalid diagnostic mode file; skipping setup");
            return false;
        }
        char buffer[16];
        const size_t size = static_cast<size_t>(status.st_size);
        size_t offset = 0;
        while (offset < size) {
            const ssize_t count = ::read(fd, buffer + offset, size - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { close(fd); LOGE("diagnostic mode read failed; skipping setup"); return false; }
            offset += static_cast<size_t>(count);
        }
        // Refuse a concurrently extended configuration instead of accepting
        // only its prefix. A mode change is applied by starting a fresh process.
        char extra;
        ssize_t count;
        do { count = ::read(fd, &extra, 1); } while (count < 0 && errno == EINTR);
        close(fd);
        if (count != 0) { LOGE("diagnostic mode changed while reading; skipping setup"); return false; }
        const auto mode = bein::parse_diagnostic_mode(std::string_view(buffer, size));
        if (!mode) { LOGE("unknown diagnostic mode; skipping setup"); return false; }
        mode_ = *mode;
        LOGI("diagnostic mode selected: %s", bein::diagnostic_mode_name(mode_));
        return true;
    }

};
} // namespace

REGISTER_ZYGISK_MODULE(BeinCastRoot)

