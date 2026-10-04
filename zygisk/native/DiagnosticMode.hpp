#pragma once

#include <optional>
#include <string_view>

namespace bein {

enum class DiagnosticMode { Loader, Engine, Cast };

// Configuration is deliberately small and exact. Invalid or absent input
// must be handled by the caller as a refusal to enable a diagnostic stage.
inline std::optional<DiagnosticMode> parse_diagnostic_mode(std::string_view input) {
    if (input.empty() || input.size() > 16 || input.find('\0') != std::string_view::npos)
        return std::nullopt;
    if (input.ends_with("\r\n")) {
        input.remove_suffix(2);
    } else if (input.ends_with('\n')) {
        input.remove_suffix(1);
    }
    if (input == "loader") return DiagnosticMode::Loader;
    if (input == "engine") return DiagnosticMode::Engine;
    if (input == "cast") return DiagnosticMode::Cast;
    return std::nullopt;
}

inline const char *diagnostic_mode_name(DiagnosticMode mode) {
    switch (mode) {
        case DiagnosticMode::Loader: return "loader";
        case DiagnosticMode::Engine: return "engine";
        case DiagnosticMode::Cast: return "cast";
    }
    return "invalid";
}

}  // namespace bein
