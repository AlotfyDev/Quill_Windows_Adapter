// ============================================================================
// Structured JSON Logging Macros (LOGJ)
// AA Spec: AA-M04-LOGJ_StructuredLogging.md
//
// REQUIREMENT: LOGJ macros delegate to Quill's QUILL_LOGJ_* macros for JSON output.
// Performance: Frontend overhead = LOG (zero JSON cost on calling thread).
// Backend serializes JSON. See BenchmarkSuite for latency targets.
// ============================================================================

#pragma once
#include <quill/LogMacros.h>

// LOGJ macros — write structured JSON log entries using Quill's native
// QUILL_LOGJ_* macros (deferred JSON formatting).
//
// PERFORMANCE: LOGJ macros use QUILL_LOGJ_* which produce named-format strings
// ("text {name}") at COMPILE TIME via QUILL_GENERATE_NAMED_FORMAT_STRING.
// At runtime, the frontend code path is identical to QUILL_LOG_* — just a level
// check and argument copy into the SPSC queue. NO JSON serialization on the
// frontend thread.
//
// JSON formatting is DEFERRED to the backend thread inside JsonFileSink::write().
//
// GUIDELINE: Use LOGJ for audit trails, compliance logs, and moderate-volume
// structured output. For high-frequency trading paths, prefer LOG with plaintext
// format to reduce backend serialization load and avoid queue pressure.
//
// Uses QUILL_LOGJ_* macros (not QUILL_LOG_*) — plain-text QUILL_LOG_*
// would NOT produce JSON output. Verified against Quill v10.0.1 API.
#define LOGJ_TRACE(logger, fmt, ...)   QUILL_LOGJ_TRACE_L3(logger, fmt, ##__VA_ARGS__)
#define LOGJ_DEBUG(logger, fmt, ...)   QUILL_LOGJ_DEBUG(logger, fmt, ##__VA_ARGS__)
#define LOGJ_INFO(logger, fmt, ...)    QUILL_LOGJ_INFO(logger, fmt, ##__VA_ARGS__)
#define LOGJ_WARN(logger, fmt, ...)    QUILL_LOGJ_WARNING(logger, fmt, ##__VA_ARGS__)
#define LOGJ_ERR(logger, fmt, ...)     QUILL_LOGJ_ERROR(logger, fmt, ##__VA_ARGS__)
#define LOGJ_CRIT(logger, fmt, ...)    QUILL_LOGJ_CRITICAL(logger, fmt, ##__VA_ARGS__)