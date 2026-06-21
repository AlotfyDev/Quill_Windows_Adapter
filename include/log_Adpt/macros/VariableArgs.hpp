// ============================================================================
// LOGV Variable-Args Macros
// AA Spec: AA-M03-LOGV_Macros.md
//
// REQUIREMENT: Pass bare identifiers, NOT string literals to LOGV macros.
// Performance: Diagnostic use only — NOT for high-frequency trading hot paths.
// ============================================================================

#pragma once
#include <quill/LogMacros.h>

// LOGV macros — structured key-value logging with variable names.
//
// LOGV uses #var preprocessor stringification: pass BARE IDENTIFIERS, not
// string literals. For example:
//   int order_id = 12345;
//   LOGV_INFO(logger, "trade", order_id, price);  // ✓ correct
// Passing "order_id" would produce escaped quotes in the key name.
//
// PERFORMANCE: These macros generate named-key-value pairs in Quill's
// internal metadata. For DIAGNOSTIC USE ONLY — NOT for the high-frequency
// trading hot path (market data ticks, order placement callbacks).
//
// Hot path: use basic LOG_INFO/LOG_DEBUG instead.
//
// NOTE: Quill's LOGV family uses QUILL_LOGV_* macros, NOT QUILL_LOG_*.
// Using QUILL_LOG_INFO for LOGV would treat the first argument as a format string,
// producing garbled output instead of structured key-value pairs.
//
// NOTE: If QUILL_DISABLE_NON_PREFIXED_MACROS is NOT defined, Quill already
// provides LOGV_INFO, LOGV_DEBUG, etc. directly via LogMacros.h. These
// wrappers are only needed when the non-prefixed macros are disabled, or
// when a shortened naming convention (LOGV_ERR vs LOGV_ERROR) is desired.
#define LOGV_TRACE(logger, ...)   QUILL_LOGV_TRACE_L3(logger, ##__VA_ARGS__)
#define LOGV_DEBUG(logger, ...)   QUILL_LOGV_DEBUG(logger, ##__VA_ARGS__)
#define LOGV_INFO(logger, ...)    QUILL_LOGV_INFO(logger, ##__VA_ARGS__)
#define LOGV_WARN(logger, ...)    QUILL_LOGV_WARNING(logger, ##__VA_ARGS__)
#define LOGV_ERR(logger, ...)     QUILL_LOGV_ERROR(logger, ##__VA_ARGS__)
#define LOGV_CRIT(logger, ...)    QUILL_LOGV_CRITICAL(logger, ##__VA_ARGS__)