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
        std::pair{"prepare", DiagnosticMode::Prepare},
        std::pair{"visibility", DiagnosticMode::Visibility},
        std::pair{"registration", DiagnosticMode::Registration},
        std::pair{"status", DiagnosticMode::Status},
        std::pair{"collection", DiagnosticMode::Collection},
        std::pair{"instrumentation", DiagnosticMode::Instrumentation},
        std::pair{"all", DiagnosticMode::All},
    };
    for (const auto &[name, mode] : valid) {
        for (const std::string_view suffix : {"", "\n", "\r\n"}) {
            const std::string input = std::string(name) + std::string(suffix);
            if (input.size() <= 16) assert(parse_diagnostic_mode(input) == mode);
            else assert(!parse_diagnostic_mode(input));
        }
        assert(std::string_view(bein::diagnostic_mode_name(mode)) == name);
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
        std::string embedded(name);
        embedded.insert(1, 1, '\0');
        assert(!parse_diagnostic_mode(embedded));
        embedded = std::string(name) + '\0';
        assert(!parse_diagnostic_mode(embedded));
        embedded += "\n";
        assert(!parse_diagnostic_mode(embedded));
    }
    for (const std::string_view input : {"", "\n", "\r\n", "Loader", "PREPARE", "All",
                                         "engine", "engine\n", "engine\r\n", "cast", "cast\n", "cast\r\n",
                                         "unknown", "disabled", "status all",
                                         "status\nall"})
        assert(!parse_diagnostic_mode(input));
    assert(!parse_diagnostic_mode(std::string(16, 'x')));
    assert(!parse_diagnostic_mode(std::string(17, 'x')));
    // The cap includes the line ending: this longest mode fits one LF, but
    // its CRLF form is 17 bytes and must be rejected rather than truncated.
    assert(parse_diagnostic_mode("instrumentation\n") == DiagnosticMode::Instrumentation);
    assert(!parse_diagnostic_mode("instrumentation\r\n"));
    assert(std::string_view(bein::diagnostic_mode_name(static_cast<DiagnosticMode>(99))) == "invalid");
    puts("Exact bounded v9 diagnostic-mode parser scenarios passed");
}
