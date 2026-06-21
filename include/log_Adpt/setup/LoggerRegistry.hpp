#pragma once
#include <quill/Logger.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace Logger_Adapter::setup {

/// @class LoggerRegistry
/// @brief Thread-safe registry for named logger lookup
/// @details Layer 3 (Stateful) - Manages logger pointer storage
///          TD-3-C01: Registry returns reference to unordered_map
///          AA-C01: LoggerRegistry::Registry() returns map; reads are read-only after init
///          AA-C05: RegistryMutex protects writes only during initialization
/// @see TD-3-C01-MultiLogger.md, AA-C05-ThreadModel.md
class LoggerRegistry {
public:
    /// @brief Get or create a named logger (thread-safe with lock during init)
    /// @param name Logger name to look up
    /// @return Logger pointer or nullptr if not found
    /// @details TD: Concurrent GetLogger from 8 threads → AA: Thread-safe via Quill internal sync
    ///          Note: Caller must have already created logger via Frontend::create_or_get_logger
    static quill::Logger* GetOrCreate(const std::string& name) {
        auto& reg = Registry();
        auto& mtx = RegistryMutex();

        std::lock_guard<std::mutex> lock(mtx);
        auto it = reg.find(name);
        if (it != reg.end()) {
            return it->second;
        }
        return nullptr;
    }

    /// @brief Get a named logger (lock-free after initialization)
    /// @param name Logger name to look up
    /// @return Logger pointer or nullptr if not found
    /// @details TD: LoggerRegistry thread safety → AA: Reads do NOT take lock (read-only after init)
    ///          This is correct because map is read-only after InitializeLogging() completes
    static quill::Logger* Get(const std::string& name) {
        auto& reg = Registry();
        auto it = reg.find(name);
        return (it != reg.end()) ? it->second : nullptr;
    }

    /// @brief Get the default (root) logger
    /// @return Root logger pointer
    /// @details TD: GetDefaultLogger returns root → AA: Returns root logger from registry
    static quill::Logger* GetDefault() {
        return Get("root");
    }

    /// @brief Check if a logger exists in registry
    /// @param name Logger name to check
    /// @return true if logger exists, false otherwise
    /// @details AA-C01: Used to verify logger registration
    static bool Exists(const std::string& name) {
        return Get(name) != nullptr;
    }

    /// @brief Register a logger pointer in the registry
    /// @param name Logger name
    /// @param logger Pointer to Quill logger
    /// @details AA-C01: Called by InitializeLogging after creating named loggers
    ///          TD: Register connects Quill logger to Adapter registry
    ///          Only used during initialization - no lock needed after init
    static void Register(const std::string& name, quill::Logger* logger) {
        auto& reg = Registry();
        auto& mtx = RegistryMutex();

        std::lock_guard<std::mutex> lock(mtx);
        reg[name] = logger;
    }

#ifdef _DEBUG
    /// @brief Reset the registry for test isolation
    /// @details TD: Test isolation → AA: ResetForTesting() guards with #ifdef _DEBUG
    ///          Allows tests to run in isolation without state leakage
    static void ResetForTesting() {
        auto& reg = Registry();
        auto& mtx = RegistryMutex();
        std::lock_guard<std::mutex> lock(mtx);
        reg.clear();
    }
#endif

    /// @brief Retrieve all currently registered loggers in a thread-safe manner
    /// @return Vector of registered logger pointers
    static std::vector<quill::Logger*> GetAllLoggers() {
        auto& reg = Registry();
        auto& mtx = RegistryMutex();
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<quill::Logger*> loggers;
        loggers.reserve(reg.size());
        for (auto& [name, ptr] : reg) {
            if (ptr) {
                loggers.push_back(ptr);
            }
        }
        return loggers;
    }

private:
    /// @brief Get the singleton registry map
    /// @return Reference to static registry map
    /// @details TD: Registry returns unordered_map → Internal use only
    ///          Function-local static ensures ODR-safe singleton
    static std::unordered_map<std::string, quill::Logger*>& Registry() {
        static std::unordered_map<std::string, quill::Logger*> registry;
        return registry;
    }

    /// @brief Get the mutex protecting registry writes
    /// @return Reference to static mutex
    /// @details TD: RegistryMutex protects writes → AA-C05: Protects writes only during initialization
    ///          Function-local static ensures ODR-safe across TUs
    static std::mutex& RegistryMutex() {
        static std::mutex mtx;
        return mtx;
    }
};

} // namespace Logger_Adapter::setup