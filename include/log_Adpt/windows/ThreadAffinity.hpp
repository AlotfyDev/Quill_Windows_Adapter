#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>

namespace Logger_Adapter::windows {

/// @struct ThreadAffinityConfig
/// @brief Configuration for Windows thread affinity
/// @details AA-P02: NUMA-aware thread affinity setup
struct ThreadAffinityConfig {
    /// Processor mask (bit 0 = processor 0).
    /// For NUMA systems, use group_affinity instead.
    DWORD_PTR processor_mask = 0;
    
    /// NUMA group number (0-based). Only used if processor_mask is non-zero.
    WORD group = 0;
    
    /// If true, use SetThreadGroupAffinity for NUMA support.
    /// Requires Windows 7+.
    /// ⚠️ On systems with >64 logical processors, use_group_affinity MUST be true.
    bool use_group_affinity = false;
};

/// @brief Apply affinity to the calling thread.
/// @param config Affinity settings
/// @return true on success, false on failure (logs via OutputDebugString).
/// @details AA-P02 Step 2: NUMA-aware SetThreadAffinity
inline bool SetThreadAffinity(const ThreadAffinityConfig& config) noexcept {
    HANDLE hThread = GetCurrentThread();
    
    if (config.use_group_affinity) {
        GROUP_AFFINITY ga;
        ga.Mask = config.processor_mask;
        ga.Group = config.group;
        ga.Reserved[0] = 0;
        ga.Reserved[1] = 0;
        ga.Reserved[2] = 0;
        
        if (!SetThreadGroupAffinity(hThread, &ga, nullptr)) {
            OutputDebugStringA("SetThreadGroupAffinity failed\n");
            return false;
        }
        return true;
    }
    
    if (config.processor_mask != 0) {
        if (!SetThreadAffinityMask(hThread, config.processor_mask)) {
            OutputDebugStringA("SetThreadAffinityMask failed\n");
            return false;
        }
    }
    return true;
}

/// @brief Pin the calling thread to a single processor.
/// @param processor_id 0-based processor index
/// @param group NUMA group number (default: 0)
/// @return true on success, false on failure
/// @details AA-P02 Convenience function
inline bool PinToProcessor(uint16_t processor_id, WORD group = 0) noexcept {
    ThreadAffinityConfig config;
    // Map processor_id to mask and check if it crosses group boundary (>64 logical processors)
    if (processor_id >= 64) {
        config.processor_mask = static_cast<DWORD_PTR>(1) << (processor_id % 64);
        config.group = group;
        config.use_group_affinity = true;
    } else {
        config.processor_mask = static_cast<DWORD_PTR>(1) << processor_id;
        config.group = group;
        config.use_group_affinity = false;
    }
    return SetThreadAffinity(config);
}

} // namespace Logger_Adapter::windows
