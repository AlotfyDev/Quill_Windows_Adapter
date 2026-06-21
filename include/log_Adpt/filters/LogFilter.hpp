// ============================================================================
// Module: LogFilter — Adapter-level Filter Interface
// AA Spec: AA-M18-LogSanitization.md (Step 2 — Filter Chain)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
//
// RATIONALE:
//   Quill's Filter::filter() returns bool only and takes immutable string_view.
//   It CANNOT modify log messages — purely accept/reject.
//   This is a cross-platform limitation (NOT Windows-specific).
//
//   Our adapter provides the ALTERNATIVE: a Filter interface that CAN modify
//   log messages. This is essential for:
//     - Sanitization (redact secrets)
//     - Structured logging augmentation
//     - Dynamic field injection
//     - Any message transformation at the backend thread
//
// INTEGRATION:
//   Used by SanitizingSink (Sink decorator) on the backend thread.
//   Does NOT use Quill's Filter API — provides its own transformation pipeline.
// ============================================================================

#pragma once
#include <string>
#include <string_view>
#include <memory>
#include <vector>

namespace Logger_Adapter::filters {

/// @brief Result of a filter operation on a log message
enum class FilterResult : uint8_t {
    /// Message passes through (possibly modified)
    Pass,
    /// Message is rejected entirely (not forwarded to sink)
    Reject,
    /// Message passes unmodified (optimization: skip string copy)
    PassThrough
};

/// @class LogFilter
/// @brief Abstract base for all adapter-level log filters
/// @details Unlike quill::Filter which only accepts/rejects, our LogFilter
///          CAN modify the log message AND/OR reject it.
///
///          This is the adapter's answer to quill::Filter's limitation:
///          We provide the full transformation capability that Quill itself
///          does not offer on any platform.
///
///          AA-M18-4: Runs on backend thread via SanitizingSink::write_log()
///          AA-M18-5: No mutex — filters are immutable after construction
class LogFilter {
public:
    virtual ~LogFilter() = default;

    /// @brief Apply filter transformation to a log message
    /// @param message The log message text (modifiable in-place)
    /// @param statement The log statement text (modifiable in-place)
    /// @return FilterResult indicating action taken
    ///
    /// @note Filter modifies message/statement IN PLACE to avoid allocations.
    ///       Return PassThrough if no modification was needed (avoids string copy upstream).
    ///       Return Reject to drop the message entirely.
    virtual FilterResult Apply(std::string& message, std::string& statement) const = 0;

    /// @brief Get filter name for diagnostics
    virtual std::string_view Name() const noexcept = 0;

    /// @brief Check if filter is enabled
    virtual bool IsEnabled() const noexcept = 0;
};

/// @class FilterChain
/// @brief Chains multiple LogFilter instances sequentially
/// @details Applies each filter in order. Any filter can reject the message.
///          If all filters pass, the (possibly modified) message is forwarded.
///
///          This is the adapter-level equivalent of Quill's filter chain,
///          but with message modification capability.
class FilterChain {
public:
    /// @brief Add a filter to the chain (takes ownership)
    /// @param filter Unique pointer to filter
    void AddFilter(std::unique_ptr<LogFilter> filter);

    /// @brief Apply all filters in sequence
    /// @param message Log message (modified in-place)
    /// @param statement Log statement (modified in-place)
    /// @return true if message should be forwarded, false if rejected
    ///
    /// @note AA-M18-5: No mutex — chain is immutable after setup
    bool ApplyAll(std::string& message, std::string& statement) const;

    /// @brief Check if chain has any filters
    bool HasFilters() const noexcept { return !filters_.empty(); }

    /// @brief Clear all filters
    void Clear();

private:
    std::vector<std::unique_ptr<LogFilter>> filters_;
};

} // namespace Logger_Adapter::filters