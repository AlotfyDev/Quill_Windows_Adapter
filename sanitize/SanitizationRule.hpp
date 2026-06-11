// ============================================================================
// Module: Sanitization Rule Definition
// AA Spec: AA-M18-LogSanitization.md (Step 1 — Define Sanitization Patterns)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Logger_Adapter::config {

enum class PatternType : uint8_t {
    Literal,
    Regex
};

struct SanitizationRule {
    std::string name;
    std::string pattern;
    std::string replacement;
    bool enabled = true;
    bool case_sensitive = true;
    PatternType type = PatternType::Literal;
};

}
