#pragma once
#include <quill/sinks/Sink.h>
#include <quill/core/MacroMetadata.h>
#include <string>
#include <string_view>
#include <atomic>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Logger_Adapter::sinks {

/// @class EventLogSink
/// @brief Windows Event Log sink implementation using ReportEventW
/// @details AA-P01: Windows native EventLog sink as replacement for SyslogSink
class EventLogSink : public quill::Sink {
    std::string source_name_;
    WORD category_id_;
    HANDLE event_source_;                     ///< Cached HANDLE from constructor
    std::atomic<bool> registered_;            ///< Thread-safe registration flag

public:
    /// @brief Constructor
    /// @param source_name The Windows EventLog registered source name
    /// @param category_id The category ID to write events under
    explicit EventLogSink(
        std::string_view source_name = "Logger_Adapter_Trading_System",
        WORD category_id = 1                  // System category
    );

    ~EventLogSink() override;

    /// @brief Logs a formatted log message to the Windows Event Log.
    /// @details Overrides the Quill v10 virtual method write_log.
    void write_log(quill::MacroMetadata const* log_metadata, uint64_t log_timestamp,
                   std::string_view thread_id, std::string_view thread_name,
                   std::string const& process_id, std::string_view logger_name,
                   quill::LogLevel log_level, std::string_view log_level_description,
                   std::string_view log_level_short_code,
                   std::vector<std::pair<std::string, std::string>> const* named_args,
                   std::string_view log_message, std::string_view log_statement) override;

    /// @brief Flushes the sink
    void flush_sink() override;

private:
    static WORD LogLevelToEventType(quill::LogLevel level);
    static WORD LogLevelToEventId(quill::LogLevel level);
    static std::wstring Utf8ToWide(const char* utf8);
};

/// @brief Verify that the EventLog source is registered and writable.
/// @param source_name Windows EventLog source name
/// @return true if the source is writable, false otherwise
bool VerifyEventLogSource(const std::string& source_name);

} // namespace Logger_Adapter::sinks
