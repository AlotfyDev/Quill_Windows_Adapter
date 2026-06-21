#pragma once
#include <cstdint>
#include <cstddef>

namespace Logger_Adapter::config {

/// @enum QueueType
/// @brief Backend queue type selection for Quill logging
/// @details AA: QueueConfig::type defaults to Bounded (recommended for production)
/// @see AA-M08-QueueConfig.md - Step 1: Populate config/QueueConfig.hpp
enum class QueueType : uint8_t {
    Bounded,    ///< Fixed-size, blocks producer when full (production-safe)
    Unbounded   ///< Grows dynamically, OOM risk under load spikes
};

/// @struct QueueConfig
/// @brief Configuration for Quill backend queue behavior
/// @details Layer 2 (POD) - Cross-language configuration DTO
///          AA: Default is Bounded/8192, Release warns on Unbounded
/// @see AA-M08-QueueConfig.md - BackendOptions wiring
struct QueueConfig {
    QueueType type = QueueType::Bounded;  ///< Queue type selection (default: Bounded)

    /// Capacity in log messages (not bytes).
    /// Minimum valid value: 1024 (values below 1024 are clamped at runtime).
    /// Trading systems: 8192 (default)
    /// Batch processing: 65536
    /// Real-time with strict latency: 4096
    /// NOTE: capacity ignored when type == Unbounded.
    size_t capacity = 8192;

    bool warn_on_unbounded_release = true;  ///< Warn if unbounded in Release builds
};

} // namespace Logger_Adapter::config