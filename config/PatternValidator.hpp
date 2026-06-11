// ============================================================================
// Module: Pattern String Validator
// AA Spec: AA-M06-CustomPatternPerSink.md (pattern grammar validation)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#pragma once
#include <string>
#include <string_view>

namespace Logger_Adapter::config {

struct ValidationResult {
    bool valid = true;
    std::string error_message;
};

ValidationResult ValidatePattern(std::string_view pattern);

}
