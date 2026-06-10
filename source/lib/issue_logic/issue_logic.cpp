// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "issue_logic.h"

#include <cmath>

namespace IssueLogic {

const char* codeToString(Code code) {
    switch (code) {
        case Code::CtPolarityFlipped:       return "ct_polarity_flipped";
        case Code::ChannelPolarityMismatch: return "channel_polarity_mismatch";
        case Code::CustomMqttConnectFailed: return "custom_mqtt_connect_failed";
        case Code::CloudMqttDisconnected:   return "cloud_mqtt_disconnected";
        case Code::InfluxDbUploadFailing:   return "influxdb_upload_failing";
        case Code::NtpNotSynced:            return "ntp_not_synced";
        case Code::PanicReboot:             return "panic_reboot";
        case Code::SafeModeActive:          return "safe_mode_active";
        case Code::HeapLow:                 return "heap_low";
        case Code::LittleFsNearFull:        return "littlefs_near_full";
        case Code::VoltageOutOfRange:       return "voltage_out_of_range";
        case Code::GridFrequencyOutOfRange: return "grid_frequency_out_of_range";
        case Code::Ade7953ReadFailures:     return "ade7953_read_failures";
        default:                            return "unknown";
    }
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
    switch (code) {
        case Code::CtPolarityFlipped:       return Severity::Info;
        case Code::ChannelPolarityMismatch: return Severity::Error;
        case Code::CustomMqttConnectFailed: return Severity::Error;
        case Code::CloudMqttDisconnected:   return Severity::Warning;
        case Code::InfluxDbUploadFailing:   return Severity::Warning;
        case Code::NtpNotSynced:            return Severity::Warning;
        case Code::PanicReboot:             return Severity::Warning;
        case Code::SafeModeActive:          return Severity::Error;
        case Code::HeapLow:                 return Severity::Warning;
        case Code::LittleFsNearFull:        return Severity::Warning;
        case Code::VoltageOutOfRange:       return Severity::Warning;
        case Code::GridFrequencyOutOfRange: return Severity::Warning;
        case Code::Ade7953ReadFailures:     return Severity::Warning;
        default:                            return Severity::Warning;
    }
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
