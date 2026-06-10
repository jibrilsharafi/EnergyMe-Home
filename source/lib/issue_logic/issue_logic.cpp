// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "issue_logic.h"

#include <cmath>

namespace IssueLogic {

// Issue catalog: one row per Code, indexed by the enum value. The static_assert
// makes a missing row a compile error instead of a silent "unknown" fallback.
struct CodeDescriptor {
    const char* str;
    Severity severity;
};

static constexpr CodeDescriptor CODE_TABLE[] = {
    {"ct_polarity_flipped",         Severity::Info},    // Code::CtPolarityFlipped
    {"channel_polarity_mismatch",   Severity::Error},   // Code::ChannelPolarityMismatch
    {"custom_mqtt_connect_failed",  Severity::Error},   // Code::CustomMqttConnectFailed
    {"cloud_mqtt_disconnected",     Severity::Warning}, // Code::CloudMqttDisconnected
    {"influxdb_upload_failing",     Severity::Warning}, // Code::InfluxDbUploadFailing
    {"ntp_not_synced",              Severity::Warning}, // Code::NtpNotSynced
    {"panic_reboot",                Severity::Warning}, // Code::PanicReboot
    {"safe_mode_active",            Severity::Error},   // Code::SafeModeActive
    {"heap_low",                    Severity::Warning}, // Code::HeapLow
    {"littlefs_near_full",          Severity::Warning}, // Code::LittleFsNearFull
    {"voltage_out_of_range",        Severity::Warning}, // Code::VoltageOutOfRange
    {"grid_frequency_out_of_range", Severity::Warning}, // Code::GridFrequencyOutOfRange
    {"ade7953_read_failures",       Severity::Warning}, // Code::Ade7953ReadFailures
};
static_assert(sizeof(CODE_TABLE) / sizeof(CODE_TABLE[0]) == (size_t)Code::Count,
              "every IssueLogic::Code needs a CODE_TABLE row");

const char* codeToString(Code code) {
    if ((uint8_t)code >= (uint8_t)Code::Count) return "unknown";
    return CODE_TABLE[(uint8_t)code].str;
}

const char* severityToString(Severity severity) {
    switch (severity) {
        case Severity::Info:    return "info";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
        default:                return "unknown";
    }
}

Severity codeSeverity(Code code) {
    if ((uint8_t)code >= (uint8_t)Code::Count) return Severity::Warning;
    return CODE_TABLE[(uint8_t)code].severity;
}

Transition step(State current, bool conditionTrue) {
    switch (current) {
        case State::Inactive:
            if (conditionTrue) return {State::ActiveUnacked, true, false};
            return {State::Inactive, false, false};
        case State::ActiveUnacked:
            if (conditionTrue) return {State::ActiveUnacked, false, false};
            return {State::ClearedUnacked, false, true};
        case State::ActiveAcked:
            if (conditionTrue) return {State::ActiveAcked, false, false};
            return {State::Inactive, false, true}; // seen and resolved - gone
        case State::ClearedUnacked:
            if (conditionTrue) return {State::ActiveUnacked, true, false}; // re-raise
            return {State::ClearedUnacked, false, false};                  // linger until ack
        default:
            return {State::Inactive, false, false};
    }
}

State applyAck(State current) {
    switch (current) {
        case State::ActiveUnacked:  return State::ActiveAcked;
        case State::ClearedUnacked: return State::Inactive;
        default:                    return current;
    }
}

uint64_t counterDelta(uint64_t previous, uint64_t current) {
    if (current < previous) return 0;
    return current - previous;
}

Evidence rateEvidence(uint64_t okDelta, uint64_t errorDelta) {
    if (okDelta > 0) return Evidence::Good;
    if (errorDelta > 0) return Evidence::Bad;
    return Evidence::None;
}

uint16_t updateStreak(uint16_t streak, Evidence evidence) {
    switch (evidence) {
        case Evidence::Bad:
            return (streak == UINT16_MAX) ? streak : (uint16_t)(streak + 1);
        case Evidence::Good:
            return 0;
        case Evidence::None:
        default:
            return streak;
    }
}

Evidence channelMismatchEvidence(uint32_t evidenceDelta, uint32_t clampedDelta,
                                 uint32_t minEvidenceReads, float minClampedFraction) {
    if (evidenceDelta < minEvidenceReads) return Evidence::None;
    float fraction = (float)clampedDelta / (float)evidenceDelta;
    return (fraction >= minClampedFraction) ? Evidence::Bad : Evidence::Good;
}

Evidence readFailureEvidence(uint64_t failureDelta, uint64_t failureThreshold) {
    return (failureDelta >= failureThreshold) ? Evidence::Bad : Evidence::Good;
}

float nearestNominalVoltage(float voltage) {
    return (std::fabs(voltage - 120.0f) < std::fabs(voltage - 230.0f)) ? 120.0f : 230.0f;
}

float nearestNominalFrequency(float frequency) {
    return (std::fabs(frequency - 50.0f) < std::fabs(frequency - 60.0f)) ? 50.0f : 60.0f;
}

Evidence voltageEvidence(float voltage, float fractionTolerance) {
    if (!(voltage > 0.0f) || std::isnan(voltage)) return Evidence::None; // no reading
    float nominal = nearestNominalVoltage(voltage);
    return (std::fabs(voltage - nominal) > nominal * fractionTolerance) ? Evidence::Bad
                                                                        : Evidence::Good;
}

Evidence frequencyEvidence(float frequency, float hzTolerance) {
    if (!(frequency > 0.0f) || std::isnan(frequency)) return Evidence::None; // no reading
    float nominal = nearestNominalFrequency(frequency);
    return (std::fabs(frequency - nominal) > hzTolerance) ? Evidence::Bad : Evidence::Good;
}

} // namespace IssueLogic
