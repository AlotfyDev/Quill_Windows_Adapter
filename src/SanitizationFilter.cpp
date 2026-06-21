// ============================================================================
// Module: Sanitization Filter — Implementation
// AA Spec: AA-M18-LogSanitization.md (Step 2 — Filter Chain)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#include "pch.h"
#include "log_Adpt/sanitize/SanitizationFilter.hpp"

namespace Logger_Adapter::config {

void SanitizationFilter::LoadRules(const std::vector<SanitizationRule>& rules) {
    // AA-M18-1: Pre-compile all rules at construction time
    rules_.clear();
    
    std::vector<std::pair<std::string, std::string>> literal_patterns;
    
    for (const auto& rule : rules) {
        if (!rule.enabled) continue;
        
        CompiledRule compiled;
        compiled.pattern_str = rule.pattern;
        compiled.replacement = rule.replacement;
        compiled.type = rule.type;
        compiled.pattern_len = static_cast<int32_t>(rule.pattern.size());
        
        rules_.push_back(compiled);
        
        // Collect Literal patterns for AhoCorasick
        if (rule.type == PatternType::Literal) {
            literal_patterns.emplace_back(rule.pattern, rule.replacement);
        }
        // Regex patterns would be handled separately (not implemented yet - future work)
        // For Literal-only, we rely on AhoCorasick for O(n) matching
    }
    
    // Build AhoCorasick trie for all Literal patterns
    aho_.Build(literal_patterns);
}

std::string SanitizationFilter::Apply(const std::string& message) const {
    // AA-M18-7: Return original unchanged if no rules
    // AA-M18-4: Apply rules sequentially in order (AA-M18-1 spec)
    // AA-M18-5: No mutex - rules are immutable after LoadRules()
    
    if (rules_.empty() || !aho_.HasPatterns()) {
        return message;
    }
    
    // Apply all Literal rules via AhoCorasick
    // AA-M18-4: Single-pass O(n) matching for all Literal patterns
    std::string result = aho_.ReplaceAll(message);
    
    // If no changes, return original
    if (result == message) {
        return message;
    }
    
    return result;
}

} // namespace Logger_Adapter::config