#include "pch.h"
#include "log_Adpt/sinks/EventLogSink.hpp"

namespace Logger_Adapter::sinks {

/// @brief Constructor
/// @details AA-P01 Step 2b: Cache HANDLE, fail gracefully if RegisterEventSourceA fails
EventLogSink::EventLogSink(std::string_view source_name, WORD category_id)
    : source_name_(source_name), category_id_(category_id)
    , event_source_(RegisterEventSourceA(nullptr, source_name_.data()))
    , registered_(event_source_ != nullptr) {
    if (!registered_) {
        DWORD err = GetLastError();
        std::string diag = "EventLogSink: RegisterEventSourceA failed for '" +
                           std::string(source_name) + "' (GLE=" + std::to_string(err) + ")\n";
        OutputDebugStringA(diag.c_str());
    }
}

EventLogSink::~EventLogSink() {
    if (registered_.load(std::memory_order_relaxed) && event_source_) {
        DeregisterEventSource(event_source_);
    }
}

void EventLogSink::flush_sink() {
    // EventLog writes are synchronous, nothing to flush
}

/// @brief Overrides write_log to report events to Windows Event Log
/// @details AA-P01 Step 2b: Unicode conversion and ReportEventW formatting
void EventLogSink::write_log(quill::MacroMetadata const* /*log_metadata*/, uint64_t /*log_timestamp*/,
                             std::string_view /*thread_id*/, std::string_view /*thread_name*/,
                             std::string const& /*process_id*/, std::string_view /*logger_name*/,
                             quill::LogLevel log_level, std::string_view /*log_level_description*/,
                             std::string_view /*log_level_short_code*/,
                             std::vector<std::pair<std::string, std::string>> const* /*named_args*/,
                             std::string_view /*log_message*/, std::string_view log_statement) {
    if (!registered_.load(std::memory_order_acquire)) return;

    // Convert std::string_view to std::string for null-termination
    std::string statement(log_statement);

    // Strip trailing newline characters
    if (!statement.empty() && statement.back() == '\n') {
        statement.pop_back();
    }
    if (!statement.empty() && statement.back() == '\r') {
        statement.pop_back();
    }

    // Convert UTF-8 to Wide String (UTF-16)
    std::wstring wmessage = Utf8ToWide(statement.c_str());

    // Truncate to ReportEventW 32KB limit (minus header safety space)
    constexpr size_t MAX_EVENT_SIZE = 32768 - 256;
    if (wmessage.size() > MAX_EVENT_SIZE) {
        wmessage.resize(MAX_EVENT_SIZE);
        static std::atomic<bool> truncation_warned{false};
        bool expected = false;
        if (truncation_warned.compare_exchange_strong(expected, true)) {
            OutputDebugStringA("EventLogSink: message truncated to 32KB limit\n");
        }
    }

    LPCWSTR strings[1] = { wmessage.c_str() };

    ReportEventW(
        event_source_,
        LogLevelToEventType(log_level),
        category_id_,
        LogLevelToEventId(log_level),
        nullptr,          // user SID
        1,                // num strings
        0,                // raw data size
        strings,
        nullptr           // raw data
    );
}

WORD EventLogSink::LogLevelToEventType(quill::LogLevel level) {
    switch (level) {
        case quill::LogLevel::Warning:    return EVENTLOG_WARNING_TYPE;
        case quill::LogLevel::Error:
        case quill::LogLevel::Critical:   return EVENTLOG_ERROR_TYPE;
        default:                           return EVENTLOG_INFORMATION_TYPE;
    }
}

WORD EventLogSink::LogLevelToEventId(quill::LogLevel level) {
    switch (level) {
        case quill::LogLevel::TraceL1:
        case quill::LogLevel::TraceL2:
        case quill::LogLevel::TraceL3: return 0;
        case quill::LogLevel::Debug:    return 1;
        case quill::LogLevel::Info:     return 2;
        case quill::LogLevel::Warning:  return 3;
        case quill::LogLevel::Error:    return 4;
        case quill::LogLevel::Critical: return 5;
        default:                        return 0;
    }
}

std::wstring EventLogSink::Utf8ToWide(const char* utf8) {
    if (!utf8 || !*utf8) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring wstr(static_cast<size_t>(len) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &wstr[0], len);
    return wstr;
}

bool VerifyEventLogSource(const std::string& source_name) {
    HANDLE h = RegisterEventSourceA(nullptr, source_name.c_str());
    if (!h) return false;
    LPCWSTR test_msg = L"Logger_Adapter EventLog source verification";
    BOOL ok = ReportEventW(h, EVENTLOG_INFORMATION_TYPE, 0, 0, nullptr, 1, 0, &test_msg, nullptr);
    DeregisterEventSource(h);
    return ok == TRUE;
}

} // namespace Logger_Adapter::sinks