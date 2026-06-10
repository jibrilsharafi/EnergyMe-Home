// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure decision logic for the device issue registry (issue #145).
//
// Everything here is free-standing: no Arduino, no FreeRTOS, no logging, no
// global state. The firmware registry (src/issueregistry.cpp) gathers runtime
// facts from the modules, feeds them through these functions on a periodic
// tick, and acts on the decisions (instance table updates, log lines, REST).
// That boundary is what lets the lifecycle and every predicate be unit-tested
// on the host (see test/test_issue_logic) without any hardware.
namespace IssueLogic {

// ============================================================================
// Issue catalog
// ============================================================================

enum class Severity : uint8_t {
    Info,    // System acted autonomously / informational - nothing to fix
    Warning, // Degraded but functioning - should be looked at
    Error,   // Data loss or functionality loss - needs fixing
};

enum class Code : uint8_t {
    CtPolarityFlipped = 0,    // channel-scoped event: auto-detection flipped the reverse flag
    ChannelPolarityMismatch,  // channel-scoped: conducting but readings persistently clamped to 0
    CustomMqttConnectFailed,  // custom MQTT enabled but cannot connect
    CloudMqttDisconnected,    // cloud services enabled but AWS MQTT down
    InfluxDbUploadFailing,    // InfluxDB enabled but uploads failing
    NtpNotSynced,             // time never synced (data timestamps unreliable)
    PanicReboot,              // event: last reset was a crash
    SafeModeActive,           // restart protection engaged
    HeapLow,                  // free internal heap below threshold
    LittleFsNearFull,         // filesystem close to capacity
    VoltageOutOfRange,        // sustained deviation from nominal mains voltage
    GridFrequencyOutOfRange,  // sustained deviation from nominal grid frequency
    Ade7953ReadFailures,      // sustained meter reading failures
    Count
};

const char* codeToString(Code code);
const char* severityToString(Severity severity);
Severity codeSeverity(Code code);

// ============================================================================
// Lifecycle (ISA-18.2-lite four-state machine)
// ============================================================================
// Two orthogonal axes: condition (active / cleared) and acknowledgement
// (unacked / acked). An instance is removed only when the condition is gone
// AND a human has seen it: ack never hides a live problem, clearing never
// hides an unseen one. Instantaneous events (CT flip, panic reboot) pulse the
// condition true for one tick and land in ClearedUnacked, lingering until
// acked. Everything is RAM-derived from current runtime signals, so a stale
// issue is impossible by construction (and a reboot wipes unseen events -
// the firmware log stays the forensic record).

enum class State : uint8_t {
    Inactive,       // no instance (not shown anywhere)
    ActiveUnacked,  // condition true, not seen - alarming
    ActiveAcked,    // condition true, seen - calm but listed (cannot be dismissed)
    ClearedUnacked, // condition gone, not seen - listed greyed until acked
};

struct Transition {
    State state;
    bool raised;  // condition edge false->true happened (log "issue raised")
    bool cleared; // condition edge true->false happened (log "issue cleared")
};

// One lifecycle step given the current condition value.
Transition step(State current, bool conditionTrue);

// User acknowledgement: ActiveUnacked -> ActiveAcked, ClearedUnacked -> Inactive.
// Other states are unchanged (acking is idempotent).
State applyAck(State current);

// True if the state is visible to the user (i.e. an instance exists).
inline bool isVisible(State s) { return s != State::Inactive; }
inline bool isActive(State s) { return s == State::ActiveUnacked || s == State::ActiveAcked; }
inline bool isUnacked(State s) { return s == State::ActiveUnacked || s == State::ClearedUnacked; }

// ============================================================================
// Window evidence helpers
// ============================================================================
// The registry tick samples monotonic counters and derives per-window deltas.
// Modules only ever increment counters (the existing Statistics pattern); all
// windowing and hysteresis lives here, centrally.

// Delta between two samples of a monotonic counter. A backwards counter
// (module restarted its counters) yields 0 - one window of no evidence,
// absorbed by the streak hysteresis.
uint64_t counterDelta(uint64_t previous, uint64_t current);

// Per-window evidence for a "configured X is failing" condition.
enum class Evidence : uint8_t {
    None, // nothing happened this window - keep current streak
    Good, // at least one success - reset streak
    Bad,  // failures and no successes - extend streak
};

Evidence rateEvidence(uint64_t okDelta, uint64_t errorDelta);

// Consecutive-bad-windows streak with no-evidence freeze. Returns the updated
// streak. Raise predicates compare the streak against a per-code threshold;
// one good window collapses it to zero (immediate clear).
uint16_t updateStreak(uint16_t streak, Evidence evidence);

// ============================================================================
// Per-code predicates
// ============================================================================

// Channel polarity mismatch: a LOAD/PV channel whose evidence-carrying readings
// (conducting, or sub-gate with |P| above the offset-noise floor) keep getting
// clamped to zero (reverse flag and physical CT orientation disagree - either
// side can be the fix, the symptom is the same). Evaluated over one tick window
// of per-channel counter deltas. Windows with fewer than minEvidenceReads
// evidence reads carry no evidence.
Evidence channelMismatchEvidence(uint32_t evidenceDelta, uint32_t clampedDelta,
                                 uint32_t minEvidenceReads, float minClampedFraction);

// Sustained meter read failures: bad when the failure delta in this window
// reaches failureThreshold, good when a window stays below it.
Evidence readFailureEvidence(uint64_t failureDelta, uint64_t failureThreshold);

// Nominal mains references (the supported grids): 120 V / 230 V, 50 Hz / 60 Hz.
float nearestNominalVoltage(float voltage);
float nearestNominalFrequency(float frequency);

// Out-of-range checks return Evidence so they plug into the same streak
// hysteresis (sustained deviation raises; one in-range window clears).
// A non-positive / absurd reading carries no evidence (meter not reading).
Evidence voltageEvidence(float voltage, float fractionTolerance);
Evidence frequencyEvidence(float frequency, float hzTolerance);

} // namespace IssueLogic
