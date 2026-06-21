// ============================================================================
// Module: Aho-Corasick Multi-Pattern Matcher
// AA Spec: AA-M18-LogSanitization.md (Step 1 — Literal pattern matching)
// Reference: thoughts.md - Correct Aho-Corasick with overlapping pattern support
// ============================================================================

#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Logger_Adapter::detail {

/// @struct AhoCorasickNode
/// @brief Single node in Aho-Corasick automaton
struct AhoCorasickNode {
    // Transitions: character -> next node index
    std::vector<std::pair<char, int32_t>> children;
    
    // Failure link (index)
    int32_t fail = 0;
    
    // Output: pattern indices that end at this node
    // Supports multiple overlapping patterns ending at same node
    std::vector<int32_t> output;
};

/// @class AhoCorasick
/// @brief Multi-pattern substring matcher using Aho-Corasick algorithm
/// @details AA-M18-4: O(n + m) runtime for all Literal patterns
///          Correctly handles overlapping patterns via output propagation
class AhoCorasick {
public:
    void Build(const std::vector<std::pair<std::string, std::string>>& patterns);
    std::string ReplaceAll(const std::string& text) const;
    
    bool HasPatterns() const noexcept { return !patterns_.empty(); }

private:
    /// @brief Find transition from node via character
    /// @param node State node index
    /// @param ch Character to transition on
    /// @return Next node index, or -1 if no direct transition
    int32_t FindTransition(int32_t node, char ch) const;
    
    /// @brief Move to next state with failure link fallback
    /// @param state Current state (node index)
    /// @param ch Current input character
    /// @return Next state after following transitions and/or failure links
    int32_t GoTo(int32_t state, char ch) const;
    
    std::vector<AhoCorasickNode> nodes_;
    std::vector<std::pair<std::string, std::string>> patterns_;
};

} // namespace Logger_Adapter::detail