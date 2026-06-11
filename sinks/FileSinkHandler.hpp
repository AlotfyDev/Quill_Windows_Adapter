// ============================================================================
// Module: File Sink Handler (append/truncate + daily rotation)
// AA Spec: AA-M01-FileSinkAppendMode.md, AA-M02-DailyFileRotation.md
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#pragma once
#include <string>
#include <chrono>
#include <cstdint>

namespace Logger_Adapter::sinks {

struct FileSinkConfig {
    std::string filename;
    bool append = true;
    bool daily_rotation = false;
    std::chrono::hours rotation_interval{24};
    uint32_t max_files = 0;
    bool enable_gzip = false;
};

void PrepareTruncate(const std::string& filename);

class FileRotationHandler {
public:
    explicit FileRotationHandler(const FileSinkConfig& config);
    std::string GetRotationPath(const std::string& base_filename);

private:
    FileSinkConfig config_;
};

}
