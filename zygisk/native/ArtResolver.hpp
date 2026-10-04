#pragma once

#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xz.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The mini-ELF contains symbol addresses for the original loaded library. Its
// section offsets must never be used as runtime addresses or load segments.
class ArtResolver {
public:
    using Segment = std::pair<uintptr_t, uintptr_t>;
    using Logger = void (*)(bool error, const char *message);
    static constexpr size_t kMaximumDebugData = 64 * 1024 * 1024;
    static constexpr size_t kMaximumCompressedDebugData = 16 * 1024 * 1024;

    explicit ArtResolver(Logger logger = nullptr) : logger_(logger) {}
    ArtResolver(const ArtResolver &) = delete;
    ArtResolver &operator=(const ArtResolver &) = delete;
    ~ArtResolver() {
        if (mapped_) munmap(mapped_, mapped_size_);
        free(decoded_);
    }

    bool open_loaded() {
        dl_iterate_phdr(find_loaded, this);
        if (path_.empty() || segments_.empty()) return fail("loaded libart.so was not found");
        const int fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            log(true, "ART resolver: cannot open libart.so (errno=%d)", errno);
            return false;
        }
        struct stat status{};
        if (fstat(fd, &status) != 0 || status.st_size <= 0 ||
            static_cast<uintmax_t>(status.st_size) > std::numeric_limits<size_t>::max()) {
            close(fd);
            return fail("invalid file size");
        }
        mapped_size_ = static_cast<size_t>(status.st_size);
        mapped_ = mmap(nullptr, mapped_size_, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (mapped_ == MAP_FAILED) {
            mapped_ = nullptr;
            return fail("mmap failed");
        }
#if defined(__aarch64__)
        constexpr uint16_t machine = EM_AARCH64;
#elif defined(__arm__)
        constexpr uint16_t machine = EM_ARM;
#elif defined(__x86_64__)
        constexpr uint16_t machine = EM_X86_64;
#elif defined(__i386__)
        constexpr uint16_t machine = EM_386;
#else
#error Unsupported resolver architecture
#endif
        return initialize(mapped_, mapped_size_, bias_, segments_, machine);
    }

    // A borrowed file image and explicit original mappings also allow host
    // fixtures to exercise the production parser without mapping ARM code.
    bool initialize(const void *image, size_t size, uintptr_t bias,
                    const std::vector<Segment> &segments, uint16_t machine) {
        if (initialized_) return fail("initialization already attempted");
        initialized_ = true;
        bias_ = bias;
        segments_ = segments;
        machine_ = machine;
        if (!image || segments_.empty()) return fail("missing image or original load segments");
        for (const auto &[begin, end] : segments_) {
            if (end <= begin) return fail("invalid original load segment");
        }
        Image outer{static_cast<const unsigned char *>(image), size};
        if (!parse(outer, dynamic_, full_, true)) return false;
        if (debug_size_) {
            log(false, "ART resolver: .gnu_debugdata present (%zu compressed bytes)", debug_size_);
            if (!decode_debugdata(outer.bytes + debug_offset_, debug_size_)) return false;
            Image inner{decoded_, decoded_size_};
            if (!parse(inner, debug_dynamic_, debug_full_, false)) {
                log(true, "ART resolver: decoded .gnu_debugdata ELF was rejected");
                return false;
            }
            log(false, "ART resolver: .gnu_debugdata decoded (%zu bytes, dynsym=%zu, symtab=%zu)",
                decoded_size_, debug_dynamic_.count, debug_full_.count);
        } else {
            log(false, "ART resolver: .gnu_debugdata absent");
        }
        if (!dynamic_.count && !full_.count && !debug_dynamic_.count && !debug_full_.count)
            return fail("no symbol tables");
        ready_ = true;
        log(false, "ART resolver ready (%s, dynsym=%zu, symtab=%zu, debug_dynsym=%zu, debug_symtab=%zu)",
            sizeof(void *) == 8 ? "64-bit" : "32-bit", dynamic_.count, full_.count,
            debug_dynamic_.count, debug_full_.count);
        log(false, "ART resolver: original ELF path=%s, GNU build ID=%s",
            path_.empty() ? "<borrowed image>" : path_.c_str(),
            build_id_.empty() ? "<absent>" : build_id_.c_str());
        return true;
    }

    // Identity belongs to the original ELF, never its compressed mini-ELF.
    const std::string &build_id() const { return build_id_; }

    void *find(std::string_view name, bool prefix = false) const {
        if (!ready_ || name.empty()) return nullptr;
        const Table *tables[] = {&dynamic_, &full_, &debug_dynamic_, &debug_full_};
        const char *sources[] = {".dynsym", ".symtab", ".gnu_debugdata/.dynsym", ".gnu_debugdata/.symtab"};
        for (size_t i = 0; i < 4; ++i) {
            if (void *result = find_in(*tables[i], name, prefix)) {
                if (name.find("GetMethodShorty") != std::string_view::npos && !shorty_logged_) {
                    shorty_logged_ = true;
                    log(false, "ART resolver: GetMethodShorty resolved from %s (%s lookup)",
                        sources[i], prefix ? "prefix" : "exact");
                }
                return result;
            }
        }
        return nullptr;
    }

private:
    struct Image {
        const unsigned char *bytes;
        size_t size;
        bool range(uint64_t offset, uint64_t count) const {
            return offset <= size && count <= size - static_cast<size_t>(offset);
        }
        template <typename T> bool read(uint64_t offset, T &value) const {
            if (!range(offset, sizeof(T))) return false;
            // Malformed section offsets may be unaligned on ARM.
            memcpy(&value, bytes + static_cast<size_t>(offset), sizeof(T));
            return true;
        }
    };
    struct Table {
        const unsigned char *symbols = nullptr;
        size_t count = 0;
        const char *strings = nullptr;
        size_t strings_size = 0;
        size_t section_count = 0;
    };
    Logger logger_;
    void *mapped_ = nullptr;
    size_t mapped_size_ = 0;
    unsigned char *decoded_ = nullptr;
    size_t decoded_size_ = 0;
    uintptr_t bias_ = 0;
    uint16_t machine_ = 0;
    std::string path_;
    std::string build_id_;
    std::vector<Segment> segments_;
    Table dynamic_, full_, debug_dynamic_, debug_full_;
    size_t debug_offset_ = 0, debug_size_ = 0;
    bool initialized_ = false, ready_ = false;
    mutable bool shorty_logged_ = false;

    template <typename... Args> void log(bool error, const char *format, Args... args) const {
        if (!logger_) return;
        char message[384];
        if constexpr (sizeof...(Args) == 0) {
            snprintf(message, sizeof(message), "%s", format);
        } else {
            snprintf(message, sizeof(message), format, args...);
        }
        logger_(error, message);
    }
    bool fail(const char *reason) const {
        log(true, "ART resolver: %s", reason);
        return false;
    }
    static const char *string_at(const char *strings, size_t size, size_t offset) {
        if (!strings || offset >= size || !memchr(strings + offset, '\0', size - offset)) return nullptr;
        return strings + offset;
    }
    bool parse(const Image &image, Table &dynamic, Table &full, bool outer) {
        ElfW(Ehdr) header{};
        constexpr unsigned char elf_class = sizeof(void *) == 8 ? ELFCLASS64 : ELFCLASS32;
        if (!image.read(0, header) || memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
            header.e_ident[EI_CLASS] != elf_class || header.e_ident[EI_DATA] != ELFDATA2LSB ||
            header.e_ident[EI_VERSION] != EV_CURRENT || header.e_version != EV_CURRENT ||
            header.e_machine != machine_ || header.e_type != ET_DYN ||
            header.e_ehsize != sizeof(ElfW(Ehdr)) || header.e_shentsize != sizeof(ElfW(Shdr)) ||
            !header.e_shnum || !image.range(header.e_shoff,
                static_cast<uint64_t>(header.e_shnum) * sizeof(ElfW(Shdr))))
            return fail("unsupported or truncated ELF header/section table");

        const char *section_names = nullptr;
        size_t section_names_size = 0;
        if (header.e_shstrndx != SHN_UNDEF) {
            ElfW(Shdr) names{};
            if (header.e_shstrndx >= header.e_shnum ||
                !image.read(header.e_shoff + static_cast<uint64_t>(header.e_shstrndx) * sizeof(names), names) ||
                names.sh_type != SHT_STRTAB || !image.range(names.sh_offset, names.sh_size))
                return fail("invalid section name table");
            section_names = reinterpret_cast<const char *>(image.bytes + names.sh_offset);
            section_names_size = static_cast<size_t>(names.sh_size);
        }
        for (size_t index = 0; index < header.e_shnum; ++index) {
            ElfW(Shdr) section{};
            if (!image.read(header.e_shoff + index * sizeof(section), section))
                return fail("truncated section header");
            const char *section_name = "";
            if (section_names) {
                section_name = string_at(section_names, section_names_size, section.sh_name);
                if (!section_name) return fail("invalid section name");
            } else if (section.sh_name != 0) {
                return fail("section name without string table");
            }
            if (outer && strcmp(section_name, ".gnu_debugdata") == 0) {
                if (debug_size_ || section.sh_type != SHT_PROGBITS || section.sh_size == 0 ||
                    section.sh_size > kMaximumCompressedDebugData ||
                    !image.range(section.sh_offset, section.sh_size))
                    return fail("invalid or oversized .gnu_debugdata section");
                debug_offset_ = static_cast<size_t>(section.sh_offset);
                debug_size_ = static_cast<size_t>(section.sh_size);
            }
            if (outer && section.sh_type == SHT_NOTE && !parse_notes(image, section))
                return false;
            if (section.sh_type != SHT_DYNSYM && section.sh_type != SHT_SYMTAB) continue;
            ElfW(Shdr) strings{};
            if (section.sh_link >= header.e_shnum || section.sh_entsize != sizeof(ElfW(Sym)) ||
                section.sh_size % sizeof(ElfW(Sym)) != 0 || !image.range(section.sh_offset, section.sh_size) ||
                !image.read(header.e_shoff + static_cast<uint64_t>(section.sh_link) * sizeof(strings), strings) ||
                strings.sh_type != SHT_STRTAB || strings.sh_size == 0 ||
                !image.range(strings.sh_offset, strings.sh_size))
                return fail("invalid symbol or symbol string table");
            Table table{image.bytes + section.sh_offset,
                        static_cast<size_t>(section.sh_size / sizeof(ElfW(Sym))),
                        reinterpret_cast<const char *>(image.bytes + strings.sh_offset),
                        static_cast<size_t>(strings.sh_size), header.e_shnum};
            if (section.sh_type == SHT_DYNSYM) dynamic = table;
            else full = table;
        }
        return true;
    }

    bool parse_notes(const Image &image, const ElfW(Shdr) &section) {
        // ELF note names and descriptions are padded to four-byte boundaries.
        // Validate the whole section before forming pointers, then advance with
        // subtraction-based bounds so uint32 note sizes cannot wrap on ARM32.
        if (!image.range(section.sh_offset, section.sh_size) ||
            section.sh_offset % 4 != 0 || section.sh_size % 4 != 0 ||
            (section.sh_addralign > 1 &&
             (section.sh_addralign < 4 ||
              (section.sh_addralign & (section.sh_addralign - 1)) != 0 ||
              section.sh_offset % section.sh_addralign != 0)))
            return fail("invalid note section range or alignment");
        const size_t begin = static_cast<size_t>(section.sh_offset);
        const size_t length = static_cast<size_t>(section.sh_size);
        size_t cursor = 0;
        while (cursor < length) {
            ElfW(Nhdr) note{};
            if (sizeof(note) > length - cursor || !image.read(begin + cursor, note))
                return fail("truncated ELF note header");
            cursor += sizeof(note);
            const size_t name_offset = cursor;
            auto advance = [&cursor, length](uint32_t count) {
                if (count > length - cursor) return false;
                cursor += count;
                const size_t padding = (4 - cursor % 4) % 4;
                if (padding > length - cursor) return false;
                cursor += padding;
                return true;
            };
            if (!advance(note.n_namesz)) return fail("truncated ELF note name or padding");
            const size_t descriptor_offset = cursor;
            if (!advance(note.n_descsz)) return fail("truncated ELF note descriptor or padding");
            if (note.n_type != NT_GNU_BUILD_ID || note.n_namesz != 4 ||
                memcmp(image.bytes + begin + name_offset, "GNU\0", 4) != 0)
                continue;
            // GNU identifiers are small hashes. Bound hex-string allocation
            // consistently on ARM32/ARM64 before doubling this length.
            if (note.n_descsz == 0 || note.n_descsz > 64)
                return fail("invalid GNU build ID descriptor size");
            static constexpr char hexadecimal[] = "0123456789abcdef";
            std::string candidate;
            candidate.reserve(static_cast<size_t>(note.n_descsz) * 2);
            for (size_t index = 0; index < note.n_descsz; ++index) {
                const unsigned char byte = image.bytes[begin + descriptor_offset + index];
                candidate.push_back(hexadecimal[byte >> 4]);
                candidate.push_back(hexadecimal[byte & 15]);
            }
            if (!build_id_.empty() && build_id_ != candidate)
                return fail("conflicting GNU build ID notes");
            build_id_ = std::move(candidate);
        }
        return true;
    }

    bool decode_debugdata(const unsigned char *compressed, size_t size) {
        // XZ_SINGLE uses the output as its dictionary. Retrying on BUF_ERROR
        // requires a new complete decode, with a strict 64 MiB output bound.
        xz_crc32_init();
        xz_crc64_init();
        xz_dec *decoder = xz_dec_init(XZ_SINGLE, 0);
        if (!decoder) return fail("cannot allocate XZ decoder");
        size_t capacity = 1024 * 1024;
        xz_ret result = XZ_MEM_ERROR;
        while (true) {
            void *buffer = realloc(decoded_, capacity);
            if (!buffer) break;
            decoded_ = static_cast<unsigned char *>(buffer);
            xz_buf input{compressed, 0, size, decoded_, 0, capacity};
            result = xz_dec_run(decoder, &input);
            if (result == XZ_STREAM_END) {
                // A debug section is exactly one XZ stream; reject hidden
                // trailing payloads, padding, and concatenated streams.
                if (input.in_pos != size || input.out_pos == 0) {
                    xz_dec_end(decoder);
                    return fail(".gnu_debugdata XZ stream has trailing data or empty output");
                }
                decoded_size_ = input.out_pos;
                xz_dec_end(decoder);
                return true;
            }
            if (result != XZ_BUF_ERROR || capacity == kMaximumDebugData) break;
            capacity *= 2;
        }
        xz_dec_end(decoder);
        log(true, "ART resolver: .gnu_debugdata XZ decoding failed (status=%d, output limit=%zu)",
            static_cast<int>(result), kMaximumDebugData);
        return false;
    }

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
        for (size_t index = 0; index < table.count; ++index) {
            ElfW(Sym) symbol{};
            memcpy(&symbol, table.symbols + index * sizeof(symbol), sizeof(symbol));
            if (symbol.st_shndx == SHN_UNDEF || symbol.st_shndx == SHN_ABS ||
                symbol.st_shndx == SHN_COMMON || symbol.st_value == 0 ||
                symbol.st_shndx >= table.section_count ||
                symbol.st_value > UINTPTR_MAX - bias_) continue;
            const char *candidate = string_at(table.strings, table.strings_size, symbol.st_name);
            if (!candidate) continue;
            const size_t length = strlen(candidate);
            if ((prefix ? length < name.size() : length != name.size()) ||
                memcmp(candidate, name.data(), name.size()) != 0) continue;
            const uintptr_t address = bias_ + symbol.st_value;
            for (const auto &[begin, end] : segments_) {
                if (address >= begin && address < end && symbol.st_size <= end - address)
                    return reinterpret_cast<void *>(address);
            }
        }
        return nullptr;
    }
};
