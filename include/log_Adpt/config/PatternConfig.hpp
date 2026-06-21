#pragma once
#include <string>

namespace Logger_Adapter::config {

/// @struct PatternConfig
/// @brief Configuration for formatting patterns and timestamp styles
/// @details AA-M06: Custom Pattern Per Sink support
struct PatternConfig {
    std::string format;                              ///< Custom format pattern string
    std::string timestamp_format = "%H:%M:%S.%Qns";  ///< Quill timestamp format (ns precision default)
    bool utc = true;                                  ///< UTC (GMT) timezone vs Local timezone
};

} // namespace Logger_Adapter::config
