// ============================================================================
// Module: Sanitization Filter (backend-thread log scrubbing)
// AA Spec: AA-M18-LogSanitization.md (Step 2 — Filter Chain)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#pragma once
#include "AhoCorasick.hpp"
#include "SanitizationRule.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace Logger_Adapter::config {

/// @class SanitizationFilter
/// @brief Applies sanitization rules to log messages on backend thread
/// @details AA-M18-4: Runs on backend thread via Sink::write() - no frontend overhead
///          AA-M18-5: No mutex in write path - rules immutable after construction
class SanitizationFilter {
public:
    /// @brief Load and compile sanitization rules
    /// @param rules Vector of sanitization rules to apply
    /// @pre Rules vector may be empty (disables filtering)
    /// @post AhoCorasick trie built for Literal rules; regex rules stored
    /// @note AA-M18-1: Called once at construction, no hot path allocation
    void LoadRules(const std::vector<SanitizationRule>& rules);

    /// @brief Apply sanitization rules to a message
    /// @param message Input log message
    /// @return Sanitized message with patterns replaced
    /// @pre Build() must be called first
    /// @post All applicable patterns replaced; original returned if no matches (AA-M18-7)
    /// @note AA-M18-4: O(input_length) worst case for typical log messages (<1KB)
    std::string Apply(const std::string& message) const;

    /// @brief Check if any rules are loaded
    /// @return true if filter has rules to apply
    /// @note AA-M18-3: Returns false when no rules - passthrough mode
    bool HasRules() const noexcept { return !rules_.empty(); }

private:
    /// @struct CompiledRule
    /// @brief Compiled rule for matching (Literal or Regex)
    struct CompiledRule {
        std::string pattern_str;
        std::string replacement;
        PatternType type = PatternType::Literal;
        int32_t pattern_len = 0;  // For Literal: pattern length
    };

    /// @brief Literal rules compiled into AhoCorasick trie
    detail::AhoCorasick aho_;

    /// @brief All rules for sequential application (AA-M18-1 rule order)
    std::vector<CompiledRule> rules_;
};

} // namespace Logger_Adapter::config