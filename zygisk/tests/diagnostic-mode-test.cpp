#include "DiagnosticMode.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

int main() {
    using bein::DiagnosticMode;
    using bein::parse_diagnostic_mode;
    constexpr std::array valid = {
        std::pair{"loader", DiagnosticMode::Loader},
        std::pair{"engine", DiagnosticMode::Engine},
        std::pair{"cast", DiagnosticMode::Cast},
    };
    for (const auto &[name, mode] : valid) {
        for (const std::string_view suffix : {"", "\n", "\r\n"}) {
            const std::string input = std::string(name) + std::string(suffix);
            assert(parse_diagnostic_mode(input) == mode);
        }
        assert(std::string_view(bein::diagnostic_mode_name(mode)) == name);
        // Partial file reads and prefixes must not accidentally activate a
        // stage. Each prefix shorter than its complete mode is rejected.
        const std::string_view input(name);
        for (size_t size = 0; size < input.size(); ++size)
            assert(!parse_diagnostic_mode(input.substr(0, size)));
        for (const std::string_view whitespace : {" ", "\t", "\r", "\v", "\f"}) {
            assert(!parse_diagnostic_mode(std::string(whitespace) + name));
            assert(!parse_diagnostic_mode(std::string(name) + std::string(whitespace)));
        }
        assert(!parse_diagnostic_mode(std::string(name) + "\n\n"));
        assert(!parse_diagnostic_mode(std::string(name) + "\r\n\r\n"));
        assert(!parse_diagnostic_mode(std::string(name) + "\n\r"));
        assert(!parse_diagnostic_mode(std::string(name) + "\n "));
        // string_view preserves these embedded NULs, unlike C-string input.
        std::string embedded(name);
        embedded.insert(1, 1, '\0');
        assert(!parse_diagnostic_mode(embedded));
        embedded = std::string(name) + '\0';
        assert(!parse_diagnostic_mode(embedded));
        embedded += "\n";
        assert(!parse_diagnostic_mode(embedded));
    }
    for (const std::string_view input : {"", "\n", "\r\n", "Loader", "ENGINE", "Cast",
                                         "unknown", "disabled", "loader engine", "cast\nengine"})
        assert(!parse_diagnostic_mode(input));
    assert(!parse_diagnostic_mode(std::string(16, 'x')));
    assert(!parse_diagnostic_mode(std::string(17, 'x')));
    assert(!parse_diagnostic_mode("engine" + std::string(10, ' ')));
    assert(!parse_diagnostic_mode("engine" + std::string(11, ' ')));
    assert(std::string_view(bein::diagnostic_mode_name(static_cast<DiagnosticMode>(99))) == "invalid");
    puts("Exact bounded diagnostic-mode parser scenarios passed");
}
