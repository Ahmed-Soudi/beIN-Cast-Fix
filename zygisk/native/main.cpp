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

// Resolve ART's hidden symbols from both .dynsym and .symtab in its actual loaded
// file. ElfW selects the correct layout for a 32-bit or 64-bit app process.
class ArtResolver {
public:
    ~ArtResolver() { if (image_) munmap(image_, size_); }
    bool open_loaded() {
        dl_iterate_phdr(find_loaded, this);
        if (path_.empty() || segments_.empty()) {
            LOGE("ART resolver: loaded libart.so was not found");
            return false;
        }
        const int fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            LOGE("ART resolver: cannot open libart.so (errno=%d)", errno);
            return false;
        }
        struct stat status{};
        if (fstat(fd, &status) != 0 || status.st_size <= 0 ||
            static_cast<uintmax_t>(status.st_size) > std::numeric_limits<size_t>::max()) {
            close(fd);
            LOGE("ART resolver: invalid file size");
            return false;
        }
        size_ = static_cast<size_t>(status.st_size);
        image_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (image_ == MAP_FAILED) {
            image_ = nullptr;
            LOGE("ART resolver: mmap failed (errno=%d)", errno);
            return false;
        }
        if (!range(0, sizeof(ElfW(Ehdr)))) return invalid("truncated ELF header");
        const auto *header = static_cast<const ElfW(Ehdr) *>(image_);
        constexpr unsigned char kElfClass = sizeof(void *) == 8 ? ELFCLASS64 : ELFCLASS32;
        if (memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
            header->e_ident[EI_CLASS] != kElfClass || header->e_ident[EI_DATA] != ELFDATA2LSB ||
            header->e_type != ET_DYN || header->e_shentsize != sizeof(ElfW(Shdr)) ||
            header->e_shnum == 0 || !range(header->e_shoff,
                static_cast<size_t>(header->e_shnum) * sizeof(ElfW(Shdr)))) {
            return invalid("unsupported ELF header/section table");
        }
        const auto *sections = reinterpret_cast<const ElfW(Shdr) *>(bytes() + header->e_shoff);
        for (size_t index = 0; index < header->e_shnum; ++index) {
            const auto &section = sections[index];
            if (section.sh_type != SHT_DYNSYM && section.sh_type != SHT_SYMTAB) continue;
            if (section.sh_link >= header->e_shnum || section.sh_entsize != sizeof(ElfW(Sym)) ||
                section.sh_size % sizeof(ElfW(Sym)) != 0 ||
                !range(section.sh_offset, section.sh_size)) continue;
            const auto &strings = sections[section.sh_link];
            if (strings.sh_type != SHT_STRTAB || !range(strings.sh_offset, strings.sh_size)) continue;
            Table table{reinterpret_cast<const ElfW(Sym) *>(bytes() + section.sh_offset),
                        static_cast<size_t>(section.sh_size / sizeof(ElfW(Sym))),
                        reinterpret_cast<const char *>(bytes() + strings.sh_offset),
                        static_cast<size_t>(strings.sh_size)};
            if (section.sh_type == SHT_DYNSYM) dynamic_ = table;
            else full_ = table;
        }
        if (!dynamic_.symbols && !full_.symbols) return invalid("no symbol tables");
        LOGI("ART resolver ready (%s, dynsym=%zu, symtab=%zu)",
             sizeof(void *) == 8 ? "64-bit" : "32-bit", dynamic_.count, full_.count);
        return true;
    }
    void *find(std::string_view name, bool prefix = false) const {
        if (void *result = find_in(dynamic_, name, prefix)) return result;
        return find_in(full_, name, prefix);
    }
private:
    struct Table {
        const ElfW(Sym) *symbols = nullptr;
        size_t count = 0;
        const char *strings = nullptr;
        size_t strings_size = 0;
    };
    void *image_ = nullptr;
    size_t size_ = 0;
    uintptr_t bias_ = 0;
    std::string path_;
    std::vector<std::pair<uintptr_t, uintptr_t>> segments_;
    Table dynamic_, full_;
    const unsigned char *bytes() const { return static_cast<const unsigned char *>(image_); }
    bool range(size_t offset, size_t count) const { return offset <= size_ && count <= size_ - offset; }
    bool invalid(const char *message) const { LOGE("ART resolver: %s", message); return false; }
    static int find_loaded(dl_phdr_info *info, size_t, void *opaque) {
        auto *self = static_cast<ArtResolver *>(opaque);
        if (!info || !info->dlpi_name) return 0;
        std::string_view name(info->dlpi_name);
        const auto slash = name.rfind('/');
        if ((slash == std::string_view::npos ? name : name.substr(slash + 1)) != "libart.so") return 0;
        self->path_ = name;
        self->bias_ = static_cast<uintptr_t>(info->dlpi_addr);
        for (size_t index = 0; index < info->dlpi_phnum; ++index) {
            const auto &segment = info->dlpi_phdr[index];
            if (segment.p_type != PT_LOAD || segment.p_memsz == 0 ||
                segment.p_vaddr > UINTPTR_MAX - self->bias_) continue;
            const uintptr_t begin = self->bias_ + segment.p_vaddr;
            if (segment.p_memsz > UINTPTR_MAX - begin) continue;
            self->segments_.emplace_back(begin, begin + segment.p_memsz);
        }
        return 1;
    }
    void *find_in(const Table &table, std::string_view name, bool prefix) const {
        if (!table.symbols || name.empty()) return nullptr;
        for (size_t index = 0; index < table.count; ++index) {
            const auto &symbol = table.symbols[index];
            if (symbol.st_shndx == SHN_UNDEF || symbol.st_value == 0 ||
                symbol.st_name >= table.strings_size || symbol.st_value > UINTPTR_MAX - bias_) continue;
            const char *candidate = table.strings + symbol.st_name;
            const void *end = memchr(candidate, '\0', table.strings_size - symbol.st_name);
            if (!end) continue;
            const size_t length = static_cast<const char *>(end) - candidate;
            if ((prefix ? length < name.size() : length != name.size()) ||
                memcmp(candidate, name.data(), name.size()) != 0) continue;
            const uintptr_t address = bias_ + symbol.st_value;
            for (const auto &[begin, finish] : segments_) {
                if (address >= begin && address < finish && symbol.st_size <= finish - address)
                    return reinterpret_cast<void *>(address);
            }
        }
        return nullptr;
    }
};

ArtResolver g_art;
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

bool initialize(JNIEnv *env, const std::vector<unsigned char> &dex) {
    LocalFrame frame(env);
    if (!frame || !load_bridge(env, dex) || !g_art.open_loaded()) return false;
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
        LOGI("selected %s (API %d, %s)", kProcess, sdk, sizeof(void *) == 8 ? "64-bit" : "32-bit");
        if (sdk != 33) {
            LOGE("prototype supports Android 13/API33 only");
            selected_ = false;
            unload();
            return;
        }
        const int directory = api_->getModuleDir();
        const bool read = directory >= 0 && read_dex(directory);
        if (directory >= 0) close(directory);
        if (!read) { selected_ = false; unload(); }
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!selected_) return;
        LOGI("app specialized; preparing Cast-only hooks");
        const bool ready = initialize(env_, dex_);
        dex_.clear();
        dex_.shrink_to_fit();
        LOGI("startup hook ready=%d", ready);
        // LSPlant Init can leave native hooks even when it fails. Do not unload
        // after attempting it; those hooks may still refer to this library.
    }
    void preServerSpecialize(zygisk::ServerSpecializeArgs *) override { unload(); }
private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool selected_ = false;
    std::vector<unsigned char> dex_;
    void unload() { if (api_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); }
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
