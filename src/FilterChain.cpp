// ============================================================================
// Module: FilterChain — Implementation
// AA Spec: AA-M18-LogSanitization.md (Step 2 — Filter Chain)
//
// RATIONALE:
//   Adapter-level alternative to quill::Filter's accept-only chain.
//   Provides message modification capability that Quill itself lacks.
// ============================================================================

#include "pch.h"
#include "log_Adpt/filters/LogFilter.hpp"

namespace Logger_Adapter::filters {

void FilterChain::AddFilter(std::unique_ptr<LogFilter> filter) {
    filters_.push_back(std::move(filter));
}

bool FilterChain::ApplyAll(std::string& message, std::string& statement) const {
    for (const auto& filter : filters_) {
        if (!filter->IsEnabled()) continue;
        
        FilterResult result = filter->Apply(message, statement);
        
        switch (result) {
            case FilterResult::Reject:
                return false;  // Message dropped
            case FilterResult::Pass:
                // Message was modified — continue chain
                break;
            case FilterResult::PassThrough:
                // Message unchanged — fast path
                break;
        }
    }
    return true;  // Forward message
}

void FilterChain::Clear() {
    filters_.clear();
}

} // namespace Logger_Adapter::filters