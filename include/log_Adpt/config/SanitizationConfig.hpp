// ============================================================================
// Module: Sanitization Configuration
// AA Spec: AA-M18-LogSanitization.md (Step 1 — Define Sanitization Patterns)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#pragma once
#include "log_Adpt/sanitize/SanitizationRule.hpp"
#include <string>
#include <vector>

namespace Logger_Adapter::config {

/// @struct SanitizationConfig
/// @brief Configuration for log message sanitization pipeline
/// @details Layer 2 (POD) - Cross-language configuration DTO
///          AA-M18-2: Master switch with default patterns for API keys/credit cards
///          AA-M18-6: Binary data limitation documented with workarounds
struct SanitizationConfig {
    /// Master switch - off by default for performance (AA-M18-3)
    bool enabled = false;

    /// Default patterns: API keys, passwords, credit cards (AA-M18-1, AA-M18-2)
    std::vector<SanitizationRule> rules = {
        {"API Key", "sk-XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX", "sk-***", true, true, PatternType::Literal},
        {"Password", "password", "***", true, false, PatternType::Literal},
        {"CreditCard", "####-####-####-####", "***-***-***-****", true, true, PatternType::Literal}
    };

    /// Binary data limitation: log messages with null bytes may leak secrets
    /// Workarounds: hex-encode binary data before logging; use sanitized byte sink
    bool sanitize_binary = false;

    /// If true, log messages containing null bytes are rejected (AA-M18-6)
    bool reject_on_null_byte = false;
};

} // namespace Logger_Adapter::config