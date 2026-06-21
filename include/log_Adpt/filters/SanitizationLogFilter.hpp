// ============================================================================
// Module: SanitizationLogFilter — LogFilter adapter for SanitizationFilter
// AA Spec: AA-M18-LogSanitization.md (Step 2 — Filter Chain)
//
// RATIONALE:
//   Wraps SanitizationFilter into the LogFilter interface so it can be used
//   in a FilterChain alongside other filters (null-byte rejection, etc.).
//
//   This is the concrete implementation of the adapter-level alternative to
//   quill::Filter. It enables message MODIFICATION (redaction) which Quill's
//   own Filter cannot do.
// ============================================================================

#pragma once
#include "log_Adpt/filters/LogFilter.hpp"
#include "log_Adpt/sanitize/SanitizationFilter.hpp"
#include <string>
#include <string_view>

namespace Logger_Adapter::filters {

/// @class SanitizationLogFilter
/// @brief LogFilter wrapper that applies SanitizationFilter rules
/// @details Transforms log messages by redacting sensitive patterns (passwords,
///          API keys, credit cards, etc.) using the Aho-Corasick algorithm.
///
///          AA-M18-4: Runs on backend thread — no frontend latency impact
///          AA-M18-5: No mutex — rules immutable after construction
class SanitizationLogFilter final : public LogFilter {
public:
    /// @brief Construct from sanitization config
    /// @param config Sanitization configuration with rules
    explicit SanitizationLogFilter(const config::SanitizationConfig& config)
        : enabled_(config.enabled) {
        if (enabled_) {
            filter_.LoadRules(config.rules);
        }
    }

    /// @brief Apply sanitization to message and statement
    /// @param message Log message (modified in-place if patterns found)
    /// @param statement Log statement (modified in-place if patterns found)
    /// @return FilterResult::Pass if modified, PassThrough if no change, Reject if binary
    FilterResult Apply(std::string& message, std::string& statement) const override {
        if (!enabled_ || !filter_.HasRules()) {
            return FilterResult::PassThrough;
        }

        bool modified = false;

        std::string result = filter_.Apply(message);
        if (result != message) {
            message = std::move(result);
            modified = true;
        }

        result = filter_.Apply(statement);
        if (result != statement) {
            statement = std::move(result);
            modified = true;
        }

        return modified ? FilterResult::Pass : FilterResult::PassThrough;
    }

    /// @brief Get filter name
    std::string_view Name() const noexcept override {
        return "SanitizationFilter";
    }

    /// @brief Check if filter is enabled
    bool IsEnabled() const noexcept override {
        return enabled_;
    }

private:
    config::SanitizationFilter filter_;
    bool enabled_;
};

} // namespace Logger_Adapter::filters