#pragma once
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include <string>
#include <type_traits>

namespace Logger_Adapter::logging {

namespace detail {

inline void append_kv_pairs(std::string&) {}

template<typename K, typename V, typename... Rest>
void append_kv_pairs(std::string& json, K&& key, V&& value, Rest&&... rest) {
    json += ",\"";
    using KClean = std::decay_t<K>;
    if constexpr (std::is_arithmetic_v<KClean>) {
        json += std::to_string(key);
    } else {
        json += key;
    }
    json += "\":\"";
    using VClean = std::decay_t<V>;
    if constexpr (std::is_arithmetic_v<VClean>) {
        json += std::to_string(value);
    } else {
        for (auto c : value) {
            switch (c) {
                case '"':  json += "\\\""; break;
                case '\\': json += "\\\\"; break;
                case '\n': json += "\\n";  break;
                case '\t': json += "\\t";  break;
                default:   json += c;      break;
            }
        }
    }
    json += "\"";
    append_kv_pairs(json, std::forward<Rest>(rest)...);
}

}

template<typename... KeyValues>
inline void LogStructured(quill::Logger* logger, quill::LogLevel level,
                          const char* event_name, KeyValues&&... kvs) {
    if (!logger) return;
    std::string json;
    json.reserve(128);
    json += R"({"event":")";
    json += event_name;
    json += "\"";
    detail::append_kv_pairs(json, std::forward<KeyValues>(kvs)...);
    json += "}";

    switch (level) {
        case quill::LogLevel::TraceL3: QUILL_LOG_TRACE_L3(logger, "{}", json); break;
        case quill::LogLevel::TraceL2: QUILL_LOG_TRACE_L2(logger, "{}", json); break;
        case quill::LogLevel::TraceL1: QUILL_LOG_TRACE_L1(logger, "{}", json); break;
        case quill::LogLevel::Debug:   QUILL_LOG_DEBUG(logger, "{}", json);    break;
        case quill::LogLevel::Info:    QUILL_LOG_INFO(logger, "{}", json);     break;
        case quill::LogLevel::Warning: QUILL_LOG_WARNING(logger, "{}", json);  break;
        case quill::LogLevel::Error:   QUILL_LOG_ERROR(logger, "{}", json);    break;
        case quill::LogLevel::Critical: QUILL_LOG_CRITICAL(logger, "{}", json); break;
        default: break;
    }
}

} // namespace Logger_Adapter::logging

