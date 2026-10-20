#pragma once

#include <optional>
#include <string_view>

namespace bein {

enum class DiagnosticMode {
    Loader, Prepare, Visibility, Registration, Status, Collection, Instrumentation, All
};

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
    if (input == "prepare") return DiagnosticMode::Prepare;
    if (input == "visibility") return DiagnosticMode::Visibility;
    if (input == "registration") return DiagnosticMode::Registration;
    if (input == "status") return DiagnosticMode::Status;
    if (input == "collection") return DiagnosticMode::Collection;
    if (input == "instrumentation") return DiagnosticMode::Instrumentation;
    if (input == "all") return DiagnosticMode::All;
    return std::nullopt;
}

inline const char *diagnostic_mode_name(DiagnosticMode mode) {
    switch (mode) {
        case DiagnosticMode::Loader: return "loader";
        case DiagnosticMode::Prepare: return "prepare";
        case DiagnosticMode::Visibility: return "visibility";
        case DiagnosticMode::Registration: return "registration";
        case DiagnosticMode::Status: return "status";
        case DiagnosticMode::Collection: return "collection";
        case DiagnosticMode::Instrumentation: return "instrumentation";
        case DiagnosticMode::All: return "all";
    }
    return "invalid";
}

}  // namespace bein

