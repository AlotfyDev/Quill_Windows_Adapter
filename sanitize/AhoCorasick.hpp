// ============================================================================
// Module: Aho-Corasick Multi-Pattern Matcher
// AA Spec: AA-M18-LogSanitization.md (Step 1 — Literal pattern matching)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#pragma once
#include <string>
#include <vector>
#include <memory>

namespace Logger_Adapter::detail {

class AhoCorasick {
public:
    void Build(const std::vector<std::pair<std::string, std::string>>& patterns);
    std::string ReplaceAll(const std::string& text) const;

private:
    // implementer: add trie node structure + failure links
};

}
