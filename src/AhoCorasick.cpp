// ============================================================================
// Module: Aho-Corasick Multi-Pattern Matcher — Implementation
// AA Spec: AA-M18-LogSanitization.md (Step 1 — Literal pattern matching)
//
// CORRECTIONS APPLIED (2026-06-21):
//   Bug #1: output changed from int32_t (single) to vector<int32_t> (multiple)
//   Bug #2: output propagation added in BFS phase for overlapping patterns
//   Fix #3: ReplaceAll rewritten with SINGLE-PASS scan (Option 2):
//           - Phase 1: One scan records ALL match (start, end, pattern_idx) triples
//           - Phase 2: Greedy leftmost-longest span merge → O(n) total
//           No more FindLongestMatch() calling GoTo from every position.
// ============================================================================

#include "pch.h"
#include "log_Adpt/sanitize/AhoCorasick.hpp"
#include <queue>
#include <vector>
#include <algorithm>

namespace Logger_Adapter::detail {

void AhoCorasick::Build(const std::vector<std::pair<std::string, std::string>>& patterns) {
    patterns_ = patterns;
    nodes_.clear();
    nodes_.push_back({});  // Root node (index 0)
    
    if (patterns_.empty()) return;
    
    // ====================================================================
    // Phase 1: Build Trie
    // ====================================================================
    for (size_t pidx = 0; pidx < patterns_.size(); ++pidx) {
        const auto& [pattern, replacement] = patterns_[pidx];
        if (pattern.empty()) continue;
        
        int32_t state = 0;
        for (char ch : pattern) {
            int32_t next = FindTransition(state, ch);
            if (next == -1) {
                next = static_cast<int32_t>(nodes_.size());
                nodes_.push_back({});
                nodes_[state].children.push_back({ch, next});
            }
            state = next;
        }
        // Mark pattern as ending at this node
        nodes_[state].output.push_back(static_cast<int32_t>(pidx));
    }
    
    // ====================================================================
    // Phase 2: Build Failure Links via BFS
    // ====================================================================
    std::queue<int32_t> q;
    
    // Initialize depth-1 nodes (children of root)
    for (const auto& [ch, idx] : nodes_[0].children) {
        nodes_[idx].fail = 0;
        q.push(idx);
    }
    
    // BFS through remaining nodes
    while (!q.empty()) {
        int32_t current = q.front();
        q.pop();
        
        for (const auto& [ch, child] : nodes_[current].children) {
            // Find failure link via current's failure chain
            int32_t fail = nodes_[current].fail;
            
            while (fail != 0 && FindTransition(fail, ch) == -1) {
                fail = nodes_[fail].fail;
            }
            
            int32_t ftrans = FindTransition(fail, ch);
            if (ftrans != -1) {
                nodes_[child].fail = ftrans;
                
                // CRITICAL: Propagate output from failure node to child
                // Ensures overlapping patterns detected (e.g., "he" inside "she")
                const auto& fail_output = nodes_[ftrans].output;
                if (!fail_output.empty()) {
                    nodes_[child].output.insert(
                        nodes_[child].output.end(),
                        fail_output.begin(),
                        fail_output.end()
                    );
                }
            } else {
                nodes_[child].fail = 0;
            }
            
            q.push(child);
        }
    }
}

int32_t AhoCorasick::FindTransition(int32_t node, char ch) const {
    for (const auto& [c, idx] : nodes_[node].children) {
        if (c == ch) return idx;
    }
    return -1;
}

int32_t AhoCorasick::GoTo(int32_t state, char ch) const {
    int32_t next = FindTransition(state, ch);
    if (next != -1) return next;
    
    // Root stays at root for missing chars
    if (state == 0) return 0;
    
    // Follow failure links until we find transition or hit root
    int32_t cur = nodes_[state].fail;
    while (cur != 0 && FindTransition(cur, ch) == -1) {
        cur = nodes_[cur].fail;
    }
    
    int32_t ftrans = FindTransition(cur, ch);
    return (ftrans != -1) ? ftrans : 0;
}

std::string AhoCorasick::ReplaceAll(const std::string& text) const {
    // AA-M18-7: Return original unchanged if no patterns or empty text
    if (patterns_.empty() || text.empty()) return text;
    
    // ====================================================================
    // Phase 1: SINGLE-PASS SCAN — record ALL matches
    // ====================================================================
    // For each match, store (start_pos, end_pos, pattern_index).
    // We collect them all in one pass using GoTo.
    //
    // struct Match tracks a (pattern_index, length) found at an end position.
    // We later convert these to start-position-based spans.
    
    struct Match {
        int32_t pattern_idx;
        size_t length;
    };
    
    // match_at_end[end_pos] = list of patterns ending here
    std::vector<std::vector<Match>> matches_at_end(text.size());
    
    int32_t state = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        state = GoTo(state, text[i]);
        
        if (!nodes_[state].output.empty()) {
            for (int32_t pidx : nodes_[state].output) {
                size_t plen = patterns_[pidx].first.size();
                if (plen > 0) {
                    matches_at_end[i].push_back({pidx, plen});
                }
            }
        }
    }
    
    // ====================================================================
    // Phase 2: Convert end-position matches to start-position spans
    // ====================================================================
    // For each start position, collect all (length, pattern_idx) pairs.
    // This enables leftmost-longest selection.
    
    // Use vector of vectors indexed by start position
    struct Span {
        size_t end;
        int32_t pattern_idx;
    };
    
    std::vector<std::vector<Span>> spans_at_start(text.size());
    
    for (size_t end = 0; end < matches_at_end.size(); ++end) {
        for (const auto& m : matches_at_end[end]) {
            size_t start = end + 1 - m.length;
            spans_at_start[start].push_back({end, m.pattern_idx});
        }
    }
    
    // ====================================================================
    // Phase 3: Greedy leftmost-longest span merge → O(n)
    // ====================================================================
    // Walk through text positions.
    // At each position, if a match starts here, pick the LONGEST and
    // emit its replacement, advancing past the match.
    // Otherwise, copy the character and advance by 1.
    
    std::string result;
    result.reserve(text.size());
    
    size_t pos = 0;
    while (pos < text.size()) {
        const auto& spans = spans_at_start[pos];
        
        if (!spans.empty()) {
            // Pick the longest match among all starting at this position
            size_t best_end = pos;
            int32_t best_pidx = -1;
            
            for (const auto& sp : spans) {
                if (sp.end >= best_end) {
                    best_end = sp.end;
                    best_pidx = sp.pattern_idx;
                }
            }
            
            if (best_pidx != -1) {
                result += patterns_[best_pidx].second;
                pos = best_end + 1;
                continue;
            }
        }
        
        // No match at this position — copy character
        result += text[pos];
        ++pos;
    }
    
    return result;
}

} // namespace Logger_Adapter::detail