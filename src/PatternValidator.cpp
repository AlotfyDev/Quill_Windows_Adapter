// ============================================================================
// Module: Pattern String Validator — Implementation
// AA Spec: AA-M06-CustomPatternPerSink.md (pattern grammar validation)
// ============================================================================

#include "pch.h"
#include "log_Adpt/config/PatternValidator.hpp"
#include <unordered_set>

namespace Logger_Adapter::config {

/// @brief Valid pattern tokens supported by Quill v10.0.1
inline const std::unordered_set<std::string_view> kValidPatternTokens = {
    "%(time)", "%(log_level)", "%(log_level_short_code)", "%(logger)",
    "%(thread_id)", "%(thread_name)", "%(file_name)", "%(full_path)",
    "%(line_number)", "%(message)", "%(caller_function)", "%(process_id)",
    "%(source_location)", "%(short_source_location)", "%(tags)", "%(named_args)",
};

/// @brief Validate a pattern string against known tokens.
/// @param pattern The pattern format string to check.
/// @return ValidationResult indicating success or descriptive error.
/// @details AA-M06 Step 4: Validates formatting tokens before passing to Quill.
ValidationResult ValidatePattern(std::string_view pattern)
{
    ValidationResult res;
    if (pattern.empty()) {
        res.valid = false;
        res.error_message = "pattern format string must not be empty";
        return res;
    }

    std::size_t pos = 0;
    while (pos < pattern.size()) {
        auto pct = pattern.find('%', pos);
        if (pct == std::string_view::npos) break;

        auto open = pattern.find('(', pct);
        if (open == std::string_view::npos || open != pct + 1) {
            ++pos;  // literal '%' or malformed token format, skip and continue
            continue;
        }

        auto close = pattern.find(')', open);
        if (close == std::string_view::npos) {
            res.valid = false;
            res.error_message = "unterminated pattern token starting at position " + std::to_string(pct);
            return res;
        }

        std::string_view token = pattern.substr(pct, close - pct + 1);
        if (kValidPatternTokens.find(token) == kValidPatternTokens.end()) {
            res.valid = false;
            res.error_message = "unknown pattern token '" + std::string(token) + "' at position " + std::to_string(pct);
            return res;
        }

        pos = close + 1;
    }

    return res;
}

} // namespace Logger_Adapter::config
