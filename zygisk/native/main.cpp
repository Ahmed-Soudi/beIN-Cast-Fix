#include <android/api-level.h>
#include <android/log.h>
#include <elf.h>
#include <fcntl.h>
#include <jni.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dobby.h>
#include <lsplant.hpp>
#include <zygisk.hpp>

#include "ArtResolver.hpp"
#include "ArtCompatibility.hpp"
#include "DiagnosticMode.hpp"

namespace {
constexpr char kLogTag[] = "BeINCastRoot";
constexpr char kProcess[] = "ptv.bein.mena";
constexpr char kBridgeClass[] = "com.soudi.beincastroot.HookBridge";
constexpr char kBuilderClass[] = "com.google.android.gms.cast.framework.CastOptions$Builder";
constexpr char kProviderClass[] = "com.netcosports.andbeinconnect.ui.chromecast.CastOptionsProvider";
constexpr size_t kMaximumDexSize = 1024 * 1024;

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
jclass g_bridge = nullptr;
jmethodID g_bridge_constructor = nullptr;
jobject g_callback = nullptr;
jfieldID g_backup_field = nullptr;
bool g_initialized = false;
std::atomic_bool g_cast_attempted{false};

void *inline_hook(void *target, void *replacement) {
    dobby_dummy_func_t original = nullptr;
    const int status = DobbyHook(target, reinterpret_cast<dobby_dummy_func_t>(replacement), &original);
    if (status != 0 || !original) {
        LOGE("ART inline hook failed (status=%d)", status);
        return nullptr;
    }
    return reinterpret_cast<void *>(original);
}
bool inline_unhook(void *target) { return DobbyDestroy(target) == 0; }

jclass load_class(JNIEnv *env, jobject loader, const char *name) {
    jclass loader_class = env->FindClass("java/lang/ClassLoader");
    if (clear_exception(env, "ClassLoader class") || !loader_class) return nullptr;
    jmethodID load = env->GetMethodID(loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (clear_exception(env, "ClassLoader.loadClass method") || !load) return nullptr;
    jstring class_name = env->NewStringUTF(name);
    if (clear_exception(env, "class name allocation") || !class_name) return nullptr;
    auto clazz = static_cast<jclass>(env->CallObjectMethod(loader, load, class_name));
    if (clear_exception(env, name) || !clazz) return nullptr;
    return clazz;
}

bool deoptimize(JNIEnv *env, jclass clazz, const char *name, const char *signature, bool is_static) {
    jmethodID id = is_static ? env->GetStaticMethodID(clazz, name, signature) :
                             env->GetMethodID(clazz, name, signature);
    if (clear_exception(env, name) || !id) return false;
    jobject method = env->ToReflectedMethod(clazz, id, is_static ? JNI_TRUE : JNI_FALSE);
    if (clear_exception(env, "deopt reflected method") || !method) return false;
    const bool success = lsplant::Deoptimize(env, method);
    if (clear_exception(env, "LSPlant.Deoptimize") || !success) {
        LOGE("deoptimize failed: %s%s", name, signature);
        return false;
    }
    LOGI("deoptimized: %s%s", name, signature);
    return true;
}

bool install_hook(JNIEnv *env, jclass clazz, const char *name, const char *signature, int kind) {
    jmethodID id = env->GetMethodID(clazz, name, signature);
    if (clear_exception(env, name) || !id) return false;
    jobject method = env->ToReflectedMethod(clazz, id, JNI_FALSE);
    if (clear_exception(env, "hook reflected method") || !method) return false;
    jobject bridge = env->NewObject(g_bridge, g_bridge_constructor, static_cast<jint>(kind));
    if (clear_exception(env, "HookBridge constructor") || !bridge) return false;
    jobject backup = lsplant::Hook(env, method, bridge, g_callback);
    if (clear_exception(env, "LSPlant.Hook") || !backup) {
        LOGE("Java hook failed: %s", name);
        return false;
    }
    // App startup is still on this thread. Publish the backup before returning
    // control to Application.attach or the provider that invokes the setter.
    env->SetObjectField(bridge, g_backup_field, backup);
    if (clear_exception(env, "publish hook backup")) {
        const bool removed = lsplant::UnHook(env, method);
        clear_exception(env, "remove incomplete hook");
        LOGE("backup assignment failed; hook removed=%d", removed);
        return false;
    }
    LOGI("Java hook installed: %s", name);
    return true;
}

void install_cast(JNIEnv *env, jclass, jobject loader) {
    if (!g_initialized || !loader || g_cast_attempted.exchange(true)) return;
    LocalFrame frame(env);
    if (!frame) return;
    LOGI("Application.attach completed; installing Cast flag hook");
    jclass provider = load_class(env, loader, kProviderClass);
    if (!provider) { LOGE("Cast provider unavailable in application classloader"); return; }
    jclass builder = load_class(env, loader, kBuilderClass);
    if (!builder) { LOGE("Cast builder unavailable in application classloader"); return; }
    if (!deoptimize(env, provider, "getCastOptions",
                    "(Landroid/content/Context;)Lcom/google/android/gms/cast/framework/CastOptions;", false)) return;
    if (!install_hook(env, builder, "setShowSystemOutputSwitcherOnCastIconClick",
                      "(Z)Lcom/google/android/gms/cast/framework/CastOptions$Builder;", 1)) return;
    LOGI("Cast hook ready: system output switcher flag will be false");
}

bool load_bridge(JNIEnv *env, const std::vector<unsigned char> &dex) {
    jclass loader_class = env->FindClass("java/lang/ClassLoader");
    if (clear_exception(env, "ClassLoader") || !loader_class) return false;
    jmethodID system_loader = env->GetStaticMethodID(loader_class, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
    if (clear_exception(env, "system classloader method") || !system_loader) return false;
    jobject parent = env->CallStaticObjectMethod(loader_class, system_loader);
    if (clear_exception(env, "system classloader") || !parent) return false;
    jclass byte_buffer = env->FindClass("java/nio/ByteBuffer");
    if (clear_exception(env, "ByteBuffer") || !byte_buffer) return false;
    jmethodID wrap = env->GetStaticMethodID(byte_buffer, "wrap", "([B)Ljava/nio/ByteBuffer;");
    if (clear_exception(env, "ByteBuffer.wrap") || !wrap) return false;
    jbyteArray data = env->NewByteArray(static_cast<jsize>(dex.size()));
    if (clear_exception(env, "DEX byte array") || !data) return false;
    env->SetByteArrayRegion(data, 0, static_cast<jsize>(dex.size()), reinterpret_cast<const jbyte *>(dex.data()));
    if (clear_exception(env, "copy DEX")) return false;
    jobject buffer = env->CallStaticObjectMethod(byte_buffer, wrap, data);
    if (clear_exception(env, "wrap DEX") || !buffer) return false;
    jclass dex_loader = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (clear_exception(env, "InMemoryDexClassLoader") || !dex_loader) return false;
    jmethodID constructor = env->GetMethodID(dex_loader, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    if (clear_exception(env, "DEX loader constructor") || !constructor) return false;
    jobject loader = env->NewObject(dex_loader, constructor, buffer, parent);
    if (clear_exception(env, "construct DEX loader") || !loader) return false;
    jclass bridge = load_class(env, loader, kBridgeClass);
    if (!bridge) return false;
    g_bridge_constructor = env->GetMethodID(bridge, "<init>", "(I)V");
    if (clear_exception(env, "bridge constructor") || !g_bridge_constructor) return false;
    g_backup_field = env->GetFieldID(bridge, "backup", "Ljava/lang/reflect/Method;");
    if (clear_exception(env, "bridge backup field") || !g_backup_field) return false;
    jmethodID callback = env->GetMethodID(bridge, "callback", "([Ljava/lang/Object;)Ljava/lang/Object;");
    if (clear_exception(env, "bridge callback") || !callback) return false;
    jobject reflected_callback = env->ToReflectedMethod(bridge, callback, JNI_FALSE);
    if (clear_exception(env, "reflect bridge callback") || !reflected_callback) return false;
    g_bridge = static_cast<jclass>(env->NewGlobalRef(bridge));
    if (clear_exception(env, "retain bridge class") || !g_bridge) return false;
    g_callback = env->NewGlobalRef(reflected_callback);
    if (clear_exception(env, "retain bridge callback") || !g_callback) return false;
    JNINativeMethod methods[] = {{const_cast<char *>("installCast"),
                                 const_cast<char *>("(Ljava/lang/ClassLoader;)V"),
                                 reinterpret_cast<void *>(install_cast)}};
    if (env->RegisterNatives(bridge, methods, 1) != JNI_OK) {
        clear_exception(env, "register installCast");
        LOGE("register installCast failed");
        return false;
    }
    LOGI("Java callback bridge ready");
    return true;
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

bool initialize(JNIEnv *env, const std::vector<unsigned char> &dex, bein::DiagnosticMode mode) {
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
        LOGE("ART preflight failed; skipped before callback bridge or native hooks");
        return false;
    }
    if (mode == bein::DiagnosticMode::Loader) {
        LOGI("diagnostic loader ready: read-only preflight complete; native module retained; no callback DEX or ART hooks");
        return true;
    }
    // Engine-only intentionally omits even callback DEX loading. The same
    // read-only preflight has already run for both isolated controls.
    if (mode == bein::DiagnosticMode::Cast && !load_bridge(env, dex)) return false;
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
    LOGI("initializing standalone ART hook engine");
    if (!lsplant::Init(env, info)) {
        clear_exception(env, "LSPlant.Init");
        LOGE("ART hook engine initialization failed; no retry");
        return false;
    }
    g_initialized = true;
    LOGI("ART hook engine ready");
    if (mode == bein::DiagnosticMode::Engine) {
        LOGI("diagnostic engine ready: no callback DEX, deoptimization, Application.attach hook, or Cast lookup");
        return true;
    }
    jclass instrumentation = env->FindClass("android/app/Instrumentation");
    if (clear_exception(env, "Instrumentation") || !instrumentation) return false;
    if (!deoptimize(env, instrumentation, "newApplication",
                    "(Ljava/lang/ClassLoader;Ljava/lang/String;Landroid/content/Context;)Landroid/app/Application;", false) ||
        !deoptimize(env, instrumentation, "newApplication",
                    "(Ljava/lang/Class;Landroid/content/Context;)Landroid/app/Application;", true)) return false;
    jclass application = env->FindClass("android/app/Application");
    if (clear_exception(env, "Application") || !application) return false;
    return install_hook(env, application, "attach", "(Landroid/content/Context;)V", 0);
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
        LOGI("selected %s (v8, API %d, %s)", kProcess, sdk, sizeof(void *) == 8 ? "64-bit" : "32-bit");
        if (sdk != 33) {
            LOGE("prototype supports Android 13/API33 only");
            selected_ = false;
            unload();
            return;
        }
        const int directory = api_->getModuleDir();
        bool read = directory >= 0 && read_mode(directory);
        if (read && mode_ == bein::DiagnosticMode::Cast) read = read_dex(directory);
        if (directory >= 0) close(directory);
        if (!read) { selected_ = false; unload(); }
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!selected_) return;
        LOGI("app specialized; diagnostic mode=%s", bein::diagnostic_mode_name(mode_));
        const bool ready = initialize(env_, dex_, mode_);
        dex_.clear();
        dex_.shrink_to_fit();
        if (mode_ == bein::DiagnosticMode::Cast) LOGI("startup hook ready=%d", ready);
        else LOGI("diagnostic stage ready=%d, mode=%s; Cast override disabled", ready,
                  bein::diagnostic_mode_name(mode_));
        // LSPlant Init can leave native hooks even when it fails. Do not unload
        // after attempting it; those hooks may still refer to this library.
    }
    void preServerSpecialize(zygisk::ServerSpecializeArgs *) override { unload(); }
private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool selected_ = false;
    bein::DiagnosticMode mode_ = bein::DiagnosticMode::Engine;
    std::vector<unsigned char> dex_;
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
    bool read_dex(int directory) {
        const int fd = openat(directory, "hook.dex", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) { LOGE("cannot open hook.dex (errno=%d)", errno); return false; }
        struct stat status{};
        if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 112 ||
            status.st_size > static_cast<off_t>(kMaximumDexSize)) {
            close(fd);
            LOGE("hook.dex has invalid type/size");
            return false;
        }
        dex_.resize(static_cast<size_t>(status.st_size));
        size_t offset = 0;
        while (offset < dex_.size()) {
            const ssize_t count = ::read(fd, dex_.data() + offset, dex_.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { close(fd); LOGE("hook.dex read failed"); return false; }
            offset += static_cast<size_t>(count);
        }
        close(fd);
        if (memcmp(dex_.data(), "dex\n", 4) != 0) { LOGE("hook.dex magic mismatch"); return false; }
        LOGI("preloaded hook.dex (%zu bytes)", dex_.size());
        return true;
    }
};
} // namespace

REGISTER_ZYGISK_MODULE(BeinCastRoot)
