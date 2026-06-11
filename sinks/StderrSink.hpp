// ============================================================================
// Module: Stderr Sink Configuration
// AA Spec: AA-M13-StderrSink.md (stderr as independent sink)
//
// == MANDATORY — IMPLEMENTER MUST ==
// 1. Document EVERY function: purpose, inputs, preconditions, postconditions
// 2. Reference the AA acceptance criterion each function implements
// 3. Follow the project coding standards (C++17, no exceptions, static lib)
// ============================================================================

#pragma once

namespace Logger_Adapter::sinks {

struct StderrSinkConfig {
    bool enabled = false;
    bool colored = true;
};

}
