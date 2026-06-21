// ============================================================================
// Module: DailyRotatingFileSink — Time-Based Log File Rotation
// AA Spec: AA-M02-DailyFileRotation.md
//
// RATIONALE:
//   Quill v10.0.1 provides RotatingSink<FileSink> with daily rotation via
//   RotatingFileSinkConfig::RotationFrequency::Daily, but it LACKS:
//     1. Gzip compression on rotation
//     2. Custom naming pattern with {YYYY-MM-DD}
//     3. Retention policy by days (max_archive_days)
//
//   This adapter implements these features by wrapping quill::FileSink
//   and providing the rotation logic ourselves.
//
// THREAD MODEL:
//   write_log() is called on Quill's BACKEND thread only (via SinkManager).
//   No mutex needed in the write path — rotation state is only modified
//   during write_log() which is single-threaded by Quill's design.
//   Compression runs on a dedicated low-priority thread.
//
// CLOCK ADJUSTMENT:
//   If the system clock jumps backward, rotation is skipped to prevent
//   archive file corruption. Documented in AA-M02 §8.
// ============================================================================

#pragma once
#include <quill/sinks/Sink.h>
#include <quill/core/MacroMetadata.h>
#include <quill/sinks/FileSink.h>

#include <string>
#include <string_view>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <cstdint>
#include <vector>

namespace Logger_Adapter::sinks {

/// @brief Configuration for daily rotating file sink
struct DailyRotationConfig {
    std::string base_filename = "logs/assembler.log";
    bool utc = true;                                   // UTC vs local timezone
    uint32_t max_archive_days = 30;                    // retention: 0 = forever
    bool gzip_on_rotation = false;                     // compress after rollover
    uint32_t gzip_level = 1;                           // zlib level 0-9 (1=fastest)
    std::chrono::seconds rotation_check_interval{60};  // how often to check time
};

/// @class DailyRotatingFileSink
/// @brief Quill sink that rotates log files at midnight (daily)
/// @details Wraps quill::FileSink and re-creates it on each new day.
///          Supports gzip compression, configurable naming, retention policy.
///
///          AA-M02-4: Retention via max_archive_days
///          AA-M02-5: Optional gzip compression on background thread
///          AA-M02-6: Approximate midnight rotation (± check interval)
///          AA-M02-8: Clock adjustment protection
class DailyRotatingFileSink : public quill::Sink {
public:
    /// @brief Constructor
    /// @param config Daily rotation configuration
    /// @param file_sink_config Quill FileSink config (applied to each new file)
    explicit DailyRotatingFileSink(
        const DailyRotationConfig& config,
        const quill::FileSinkConfig& file_sink_config = quill::FileSinkConfig{});

    /// @brief Destructor — signals compression thread and joins
    ~DailyRotatingFileSink() override;

    /// @brief Write log record — checks rotation then delegates
    void write_log(quill::MacroMetadata const* log_metadata, uint64_t log_timestamp,
                   std::string_view thread_id, std::string_view thread_name,
                   std::string const& process_id, std::string_view logger_name,
                   quill::LogLevel log_level, std::string_view log_level_description,
                   std::string_view log_level_short_code,
                   std::vector<std::pair<std::string, std::string>> const* named_args,
                   std::string_view log_message, std::string_view log_statement) override;

    /// @brief Flush current file sink
    void flush_sink() override;

private:
    /// @brief Generate today's date string (YYYY-MM-DD)
    std::string GenerateDateString() const;

    /// @brief Generate archive filename for a given date
    std::string GenerateArchiveFilename(const std::string& date_str) const;

    /// @brief Check if rotation is needed and perform it
    void CheckAndRotate(uint64_t timestamp_ns);

    /// @brief Perform the actual file rotation
    void Rotate(const std::string& new_date_str);

    /// @brief Delete archives older than max_archive_days
    void CleanupOldArchives();

    /// @brief Enqueue a file for background gzip compression
    void EnqueueCompression(const std::string& path);

    /// @brief Compression worker thread loop
    void CompressionWorker();

    /// @brief Drain pending compression tasks (shutdown)
    void DrainCompressionQueue(std::chrono::milliseconds timeout = std::chrono::seconds(5));

    // Configuration
    DailyRotationConfig config_;
    quill::FileSinkConfig file_sink_config_;

    // Current state
    std::unique_ptr<quill::FileSink> current_sink_;
    std::string current_date_str_;
    uint64_t last_rotation_check_ns_{0};

    // Compression thread
    std::thread compression_thread_;
    std::mutex compression_mtx_;
    std::condition_variable compression_cv_;
    std::deque<std::string> compression_queue_;
    std::atomic<bool> compression_shutdown_{false};
    bool compression_enabled_{true};
};

} // namespace Logger_Adapter::sinks