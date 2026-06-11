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
#include "SanitizationRule.hpp"
#include "AhoCorasick.hpp"
#include <string>
#include <vector>
#include <memory>

namespace Logger_Adapter::config {

class SanitizationFilter {
public:
    void LoadRules(const std::vector<SanitizationRule>& rules);
    std::string Apply(const std::string& message) const;
    bool HasRules() const noexcept;

private:
    // implementer: add AhoCorasick trie + regex rules here
};

}
