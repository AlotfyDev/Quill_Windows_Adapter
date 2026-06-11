// ============================================================================
// Module: Global Rate Limiter
// AA Spec: AA-M05-RateLimiting.md (§2 — Global Rate Limiter)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#pragma once
#include <cstdint>
#include <chrono>

namespace Logger_Adapter {

class RateLimiter {
public:
    bool Allow();

private:
    // implementer: add state here
};

RateLimiter& GetGlobalRateLimiter();

}
