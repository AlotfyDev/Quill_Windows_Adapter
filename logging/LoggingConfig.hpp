#pragma once
#include <chrono>
#include <string>
#include <vector>
#include <csignal> // For signal constants

namespace Logger_Adapter::logging {

struct LoggingConfig {
    std::string backend_thread_name = "LogBackend";
    std::chrono::microseconds sleep_duration{100};
    bool enable_yield_when_idle = false;

    bool enable_signal_handler = true;
    std::vector<int> catchable_signals = {SIGTERM, SIGINT, SIGABRT, SIGFPE, SIGSEGV};

    struct {
        bool enabled = true;
        std::string stream = "stdout";
        bool colored = true;
    } console;

    struct {
        bool enabled = false;
        std::string filename = "logs/assembler.log";
        bool rotating = true;
        size_t max_file_size = 10 * 1024 * 1024;
        uint32_t max_files = 5;
    } file;

    struct {
        bool enabled = false;
        std::string filename = "logs/assembler.json";
    } json;

    int default_log_level = 3;
};

}
