#pragma once
#include <cstdint>

namespace Logger_Adapter::connector_errors {

enum class ErrorCode : uint32_t {
    // General (0-99)
    Success = 0,
    Unknown = 1,
    NotImplemented = 2,

    // Configuration errors (100-199)
    ConfigInvalid = 100,
    ConfigMissingField = 101,
    ConfigSerializationMismatch = 102,
    ConfigOutOfRange = 103,

    // Connector errors (200-299) — both inbound and outbound
    ConnectorNotBound = 200,
    ConnectorCallbackNull = 201,
    ConnectorTimeout = 202,
    ConnectorSendFailed = 203,

    // Invoker errors (300-399) — outbound specific
    InvokerTargetUnreachable = 300,
    InvokerSerializationFailed = 301,
    InvokerContextInvalid = 302,
    InvokerTargetBusy = 303,

    // Assembly errors (400-499) — inbound specific
    AssemblyEnvelopeIncomplete = 400,
    AssemblyMetadataInjectionFailed = 401,
    AssemblyTimestampStabilizationFailed = 402,
    AssemblyNullDependency = 403,

    // Content errors (500-599)
    ContentParseFailed = 500,
    ContentSchemaMismatch = 501,
    ContentValidationFailed = 502,
    ContentUnknownType = 503,

    // Runtime errors (600-699)
    ResourceExhausted = 600,
    QueueFull = 601,
    ThreadPoolFull = 602,
    AllocationFailed = 603,

    // Emergency (700-799)
    EmergencyShutdown = 700,
    SignalReceived = 701,
    FatalError = 702,
};

inline constexpr const char* ErrorCodeToString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Success: return "Success";
        case ErrorCode::Unknown: return "Unknown error";
        case ErrorCode::NotImplemented: return "Not implemented";
        case ErrorCode::ConfigInvalid: return "Invalid configuration";
        case ErrorCode::ConfigMissingField: return "Missing configuration field";
        case ErrorCode::ConfigSerializationMismatch: return "Configuration serialization mismatch";
        case ErrorCode::ConfigOutOfRange: return "Configuration out of range";
        case ErrorCode::ConnectorNotBound: return "Connector not bound";
        case ErrorCode::ConnectorCallbackNull: return "Connector callback is null";
        case ErrorCode::ConnectorTimeout: return "Connector timeout";
        case ErrorCode::ConnectorSendFailed: return "Connector send failed";
        case ErrorCode::InvokerTargetUnreachable: return "Invoker target unreachable";
        case ErrorCode::InvokerSerializationFailed: return "Invoker serialization failed";
        case ErrorCode::InvokerContextInvalid: return "Invoker context invalid";
        case ErrorCode::InvokerTargetBusy: return "Invoker target busy";
        case ErrorCode::AssemblyEnvelopeIncomplete: return "Envelope incomplete";
        case ErrorCode::AssemblyMetadataInjectionFailed: return "Metadata injection failed";
        case ErrorCode::AssemblyTimestampStabilizationFailed: return "Timestamp stabilization failed";
        case ErrorCode::AssemblyNullDependency: return "Assembly null dependency";
        case ErrorCode::ContentParseFailed: return "Content parse failed";
        case ErrorCode::ContentSchemaMismatch: return "Content schema mismatch";
        case ErrorCode::ContentValidationFailed: return "Content validation failed";
        case ErrorCode::ContentUnknownType: return "Content unknown type";
        case ErrorCode::ResourceExhausted: return "Resource exhausted";
        case ErrorCode::QueueFull: return "Queue full";
        case ErrorCode::ThreadPoolFull: return "Thread pool full";
        case ErrorCode::AllocationFailed: return "Allocation failed";
        case ErrorCode::EmergencyShutdown: return "Emergency shutdown";
        case ErrorCode::SignalReceived: return "Signal received";
        case ErrorCode::FatalError: return "Fatal error";
        default: return "Unknown error code";
    }
}

} // namespace
