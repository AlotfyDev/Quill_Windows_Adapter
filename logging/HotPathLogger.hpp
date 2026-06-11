#pragma once
#include <quill/LogMacros.h>
#include <quill/Logger.h>

namespace Logger_Adapter::logging {

using LogLevel = quill::LogLevel;

#define LOG_TRACE(logger, fmt, ...)   QUILL_LOG_TRACE_L3(logger, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(logger, fmt, ...)   QUILL_LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define LOG_INFO(logger, fmt, ...)    QUILL_LOG_INFO(logger, fmt, ##__VA_ARGS__)
#define LOG_WARN(logger, fmt, ...)    QUILL_LOG_WARNING(logger, fmt, ##__VA_ARGS__)
#define LOG_ERR(logger, fmt, ...)     QUILL_LOG_ERROR(logger, fmt, ##__VA_ARGS__)
#define LOG_CRIT(logger, fmt, ...)    QUILL_LOG_CRITICAL(logger, fmt, ##__VA_ARGS__)

template<typename... Args>
inline void LogTrace(quill::Logger* logger, const char* fmt, Args&&... args) {
    QUILL_LOG_TRACE_L3(logger, fmt, std::forward<Args>(args)...);
}
template<typename... Args>
inline void LogDebug(quill::Logger* logger, const char* fmt, Args&&... args) {
    QUILL_LOG_DEBUG(logger, fmt, std::forward<Args>(args)...);
}
template<typename... Args>
inline void LogInfo(quill::Logger* logger, const char* fmt, Args&&... args) {
    QUILL_LOG_INFO(logger, fmt, std::forward<Args>(args)...);
}
template<typename... Args>
inline void LogWarning(quill::Logger* logger, const char* fmt, Args&&... args) {
    QUILL_LOG_WARNING(logger, fmt, std::forward<Args>(args)...);
}
template<typename... Args>
inline void LogError(quill::Logger* logger, const char* fmt, Args&&... args) {
    QUILL_LOG_ERROR(logger, fmt, std::forward<Args>(args)...);
}
template<typename... Args>
inline void LogCritical(quill::Logger* logger, const char* fmt, Args&&... args) {
    QUILL_LOG_CRITICAL(logger, fmt, std::forward<Args>(args)...);
}

}

