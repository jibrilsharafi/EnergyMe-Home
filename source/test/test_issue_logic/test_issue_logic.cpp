// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for the pure issue-registry logic. Run with:  pio test -e native
// (On Windows the native toolchain is unreliable - run it from WSL.)
//
// These exercise the ISA-18.2-lite lifecycle (active/cleared x unacked/acked),
// the window-evidence helpers and every per-code predicate entirely in memory.
// The scenario tests at the bottom replay the design discussions for issue #145:
// a correct auto-flip (info event, lingers until ack), a wrong manual reverse
// (ongoing error that only a real fix clears), and a transient outage that
// self-heals overnight (stays visible greyed until acked).

#include <unity.h>
#include <cstring>
#include "issue_logic.h"

using namespace IssueLogic;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Catalog
// ============================================================================

void test_code_strings_are_unique_and_stable(void) {
    // Every code has a non-"unknown" string; spot-check the documented ones.
    for (uint8_t i = 0; i < (uint8_t)Code::Count; i++) {
        TEST_ASSERT_NOT_EQUAL(0, strcmp(codeToString((Code)i), "unknown"));
    }
    TEST_ASSERT_EQUAL_STRING("ct_polarity_flipped", codeToString(Code::CtPolarityFlipped));
    TEST_ASSERT_EQUAL_STRING("channel_polarity_mismatch", codeToString(Code::ChannelPolarityMismatch));
    TEST_ASSERT_EQUAL_STRING("panic_reboot", codeToString(Code::PanicReboot));
}

void test_severity_mapping(void) {
    TEST_ASSERT_EQUAL(Severity::Info, codeSeverity(Code::CtPolarityFlipped));
    TEST_ASSERT_EQUAL(Severity::Error, codeSeverity(Code::ChannelPolarityMismatch));
    TEST_ASSERT_EQUAL(Severity::Warning, codeSeverity(Code::CustomMqttConnectFailed));
    TEST_ASSERT_EQUAL(Severity::Warning, codeSeverity(Code::CloudMqttDisconnected));
    TEST_ASSERT_EQUAL(Severity::Warning, codeSeverity(Code::InfluxDbUploadFailing));
    TEST_ASSERT_EQUAL(Severity::Error, codeSeverity(Code::SafeModeActive));
    TEST_ASSERT_EQUAL(Severity::Warning, codeSeverity(Code::HeapLow));
}

// ============================================================================
// Lifecycle state machine
// ============================================================================

void test_step_raise_from_inactive(void) {
    Transition t = step(State::Inactive, true);
    TEST_ASSERT_EQUAL(State::ActiveUnacked, t.state);
    TEST_ASSERT_TRUE(t.raised);
    TEST_ASSERT_FALSE(t.cleared);
}

void test_step_inactive_stays_inactive(void) {
    Transition t = step(State::Inactive, false);
    TEST_ASSERT_EQUAL(State::Inactive, t.state);
    TEST_ASSERT_FALSE(t.raised);
    TEST_ASSERT_FALSE(t.cleared);
}

void test_step_active_unacked_clears_to_cleared_unacked(void) {
    // Condition resolves before anyone saw it: must NOT vanish.
    Transition t = step(State::ActiveUnacked, false);
    TEST_ASSERT_EQUAL(State::ClearedUnacked, t.state);
    TEST_ASSERT_TRUE(t.cleared);
}

void test_step_active_acked_clears_to_inactive(void) {
    // Seen and resolved: gone.
    Transition t = step(State::ActiveAcked, false);
    TEST_ASSERT_EQUAL(State::Inactive, t.state);
    TEST_ASSERT_TRUE(t.cleared);
}

void test_step_cleared_unacked_lingers(void) {
    Transition t = step(State::ClearedUnacked, false);
    TEST_ASSERT_EQUAL(State::ClearedUnacked, t.state);
    TEST_ASSERT_FALSE(t.raised);
    TEST_ASSERT_FALSE(t.cleared);
}

void test_step_cleared_unacked_reraises(void) {
    Transition t = step(State::ClearedUnacked, true);
    TEST_ASSERT_EQUAL(State::ActiveUnacked, t.state);
    TEST_ASSERT_TRUE(t.raised);
}

void test_ack_active_unacked_becomes_acked_not_removed(void) {
    // Acking a live problem must NOT hide it.
    TEST_ASSERT_EQUAL(State::ActiveAcked, applyAck(State::ActiveUnacked));
}

void test_ack_cleared_unacked_removes(void) {
    TEST_ASSERT_EQUAL(State::Inactive, applyAck(State::ClearedUnacked));
}

void test_ack_is_idempotent(void) {
    TEST_ASSERT_EQUAL(State::ActiveAcked, applyAck(State::ActiveAcked));
    TEST_ASSERT_EQUAL(State::Inactive, applyAck(State::Inactive));
}

void test_state_predicates(void) {
    TEST_ASSERT_FALSE(isVisible(State::Inactive));
    TEST_ASSERT_TRUE(isVisible(State::ClearedUnacked));
    TEST_ASSERT_TRUE(isActive(State::ActiveAcked));
    TEST_ASSERT_FALSE(isActive(State::ClearedUnacked));
    TEST_ASSERT_TRUE(isUnacked(State::ActiveUnacked));
    TEST_ASSERT_TRUE(isUnacked(State::ClearedUnacked));
    TEST_ASSERT_FALSE(isUnacked(State::ActiveAcked));
}

// ============================================================================
// Window evidence helpers
// ============================================================================

void test_counter_delta_normal(void) {
    TEST_ASSERT_EQUAL_UINT64(5, counterDelta(10, 15));
    TEST_ASSERT_EQUAL_UINT64(0, counterDelta(10, 10));
}

void test_counter_delta_backwards_is_zero(void) {
    // Counter reset (module restart) must not produce a giant bogus delta.
    TEST_ASSERT_EQUAL_UINT64(0, counterDelta(100, 3));
}

void test_rate_evidence(void) {
    TEST_ASSERT_EQUAL(Evidence::Good, rateEvidence(5, 2));  // any success wins
    TEST_ASSERT_EQUAL(Evidence::Bad, rateEvidence(0, 3));
    TEST_ASSERT_EQUAL(Evidence::None, rateEvidence(0, 0)); // idle window: no evidence
}

void test_streak_accumulates_and_resets(void) {
    uint16_t s = 0;
    s = updateStreak(s, Evidence::Bad);
    s = updateStreak(s, Evidence::Bad);
    TEST_ASSERT_EQUAL_UINT16(2, s);
    s = updateStreak(s, Evidence::None); // freeze, not reset
    TEST_ASSERT_EQUAL_UINT16(2, s);
    s = updateStreak(s, Evidence::Good);
    TEST_ASSERT_EQUAL_UINT16(0, s);
}

void test_streak_saturates(void) {
    uint16_t s = UINT16_MAX;
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, updateStreak(s, Evidence::Bad));
}

// ============================================================================
// Per-code predicates
// ============================================================================

void test_channel_mismatch_requires_minimum_reads(void) {
    // 3 evidence reads with min 5: window carries no evidence either way.
    TEST_ASSERT_EQUAL(Evidence::None, channelMismatchEvidence(3, 3, 5, 0.9f));
}

void test_channel_mismatch_bad_when_mostly_clamped(void) {
    TEST_ASSERT_EQUAL(Evidence::Bad, channelMismatchEvidence(20, 19, 5, 0.9f));
    TEST_ASSERT_EQUAL(Evidence::Bad, channelMismatchEvidence(20, 20, 5, 0.9f));
}

void test_channel_mismatch_good_when_readings_flow(void) {
    // Normal operation: evidence reads with zero (or few) clamps.
    TEST_ASSERT_EQUAL(Evidence::Good, channelMismatchEvidence(20, 0, 5, 0.9f));
    TEST_ASSERT_EQUAL(Evidence::Good, channelMismatchEvidence(20, 10, 5, 0.9f));
}

void test_read_failure_evidence(void) {
    TEST_ASSERT_EQUAL(Evidence::Bad, readFailureEvidence(30, 30));
    TEST_ASSERT_EQUAL(Evidence::Good, readFailureEvidence(29, 30));
    TEST_ASSERT_EQUAL(Evidence::Good, readFailureEvidence(0, 30));
}

void test_nominal_voltage_selection(void) {
    TEST_ASSERT_EQUAL_FLOAT(230.0f, nearestNominalVoltage(235.0f));
    TEST_ASSERT_EQUAL_FLOAT(120.0f, nearestNominalVoltage(118.0f));
    TEST_ASSERT_EQUAL_FLOAT(120.0f, nearestNominalVoltage(110.0f));
}

void test_nominal_frequency_selection(void) {
    TEST_ASSERT_EQUAL_FLOAT(50.0f, nearestNominalFrequency(49.8f));
    TEST_ASSERT_EQUAL_FLOAT(60.0f, nearestNominalFrequency(59.2f));
}

void test_voltage_evidence(void) {
    TEST_ASSERT_EQUAL(Evidence::Good, voltageEvidence(230.0f, 0.10f));
    TEST_ASSERT_EQUAL(Evidence::Good, voltageEvidence(215.0f, 0.10f));
    TEST_ASSERT_EQUAL(Evidence::Bad, voltageEvidence(195.0f, 0.10f));  // -15% on 230
    TEST_ASSERT_EQUAL(Evidence::Bad, voltageEvidence(140.0f, 0.10f));  // +17% on 120
    TEST_ASSERT_EQUAL(Evidence::None, voltageEvidence(0.0f, 0.10f));   // no reading
    TEST_ASSERT_EQUAL(Evidence::None, voltageEvidence(-5.0f, 0.10f));
}

void test_frequency_evidence(void) {
    TEST_ASSERT_EQUAL(Evidence::Good, frequencyEvidence(50.05f, 1.0f));
    TEST_ASSERT_EQUAL(Evidence::Bad, frequencyEvidence(48.5f, 1.0f));
    TEST_ASSERT_EQUAL(Evidence::Good, frequencyEvidence(60.4f, 1.0f));
    TEST_ASSERT_EQUAL(Evidence::None, frequencyEvidence(0.0f, 1.0f));
}

// ============================================================================
// Scenario walkthroughs (from the #145 design discussion)
// ============================================================================

// Case 1: CT auto-flip on first load. The registry pulses the condition true
// for exactly one tick (flip counter advanced); the notice must land in
// ClearedUnacked and linger until the user acks it.
void test_scenario_auto_flip_event_lingers_until_ack(void) {
    State s = State::Inactive;

    Transition t = step(s, true); // tick sees a new flip record
    TEST_ASSERT_TRUE(t.raised);
    s = t.state;

    t = step(s, false); // next tick: event over
    s = t.state;
    TEST_ASSERT_EQUAL(State::ClearedUnacked, s);

    // Days pass: it lingers, never silently disappears.
    for (int i = 0; i < 1000; i++) s = step(s, false).state;
    TEST_ASSERT_EQUAL(State::ClearedUnacked, s);

    s = applyAck(s);
    TEST_ASSERT_EQUAL(State::Inactive, s);
}

// Case 2: wrong manual reverse override. The mismatch condition stays active
// through an ack (cannot be dismissed) and resolves only when the user fixes
// either the flag or the physical CT - both collapse the clamped fraction.
void test_scenario_manual_reverse_mismatch_until_fixed(void) {
    uint16_t streak = 0;
    State s = State::Inactive;
    const uint16_t raiseThreshold = 3;

    // Wrong reverse: every window mostly clamped while carrying evidence.
    for (int tick = 0; tick < 5; tick++) {
        Evidence e = channelMismatchEvidence(30, 30, 5, 0.9f);
        streak = updateStreak(streak, e);
        s = step(s, streak >= raiseThreshold).state;
    }
    TEST_ASSERT_EQUAL(State::ActiveUnacked, s);

    // User acks but does not fix: stays listed, calm, NOT removed.
    s = applyAck(s);
    TEST_ASSERT_EQUAL(State::ActiveAcked, s);
    Evidence e = channelMismatchEvidence(30, 30, 5, 0.9f);
    streak = updateStreak(streak, e);
    s = step(s, streak >= raiseThreshold).state;
    TEST_ASSERT_EQUAL(State::ActiveAcked, s);

    // Fix (either unchecking reverse or physically flipping the CT): clamps stop.
    e = channelMismatchEvidence(30, 0, 5, 0.9f);
    streak = updateStreak(streak, e);
    s = step(s, streak >= raiseThreshold).state;
    TEST_ASSERT_EQUAL(State::Inactive, s); // was acked -> removed on clear
}

// Case 3: InfluxDB outage at night that self-heals before morning. The user
// must still find the trace (ClearedUnacked) and remove it with one ack.
void test_scenario_overnight_outage_leaves_trace(void) {
    uint16_t streak = 0;
    State s = State::Inactive;
    const uint16_t raiseThreshold = 3;

    // Outage: errors, no successes.
    for (int tick = 0; tick < 4; tick++) {
        streak = updateStreak(streak, rateEvidence(0, 5));
        s = step(s, streak >= raiseThreshold).state;
    }
    TEST_ASSERT_EQUAL(State::ActiveUnacked, s);

    // Server back: one good window clears immediately.
    streak = updateStreak(streak, rateEvidence(7, 0));
    s = step(s, streak >= raiseThreshold).state;
    TEST_ASSERT_EQUAL(State::ClearedUnacked, s);

    // Morning: user acks the trace away.
    s = applyAck(s);
    TEST_ASSERT_EQUAL(State::Inactive, s);
}

// A flapping condition must re-raise (and would re-log) on each recurrence.
void test_scenario_flapping_reraises(void) {
    State s = State::Inactive;
    s = step(s, true).state;
    s = step(s, false).state;
    TEST_ASSERT_EQUAL(State::ClearedUnacked, s);
    Transition t = step(s, true);
    TEST_ASSERT_TRUE(t.raised);
    TEST_ASSERT_EQUAL(State::ActiveUnacked, t.state);
}

// ============================================================================

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_code_strings_are_unique_and_stable);
    RUN_TEST(test_severity_mapping);

    RUN_TEST(test_step_raise_from_inactive);
    RUN_TEST(test_step_inactive_stays_inactive);
    RUN_TEST(test_step_active_unacked_clears_to_cleared_unacked);
    RUN_TEST(test_step_active_acked_clears_to_inactive);
    RUN_TEST(test_step_cleared_unacked_lingers);
    RUN_TEST(test_step_cleared_unacked_reraises);
    RUN_TEST(test_ack_active_unacked_becomes_acked_not_removed);
    RUN_TEST(test_ack_cleared_unacked_removes);
    RUN_TEST(test_ack_is_idempotent);
    RUN_TEST(test_state_predicates);

    RUN_TEST(test_counter_delta_normal);
    RUN_TEST(test_counter_delta_backwards_is_zero);
    RUN_TEST(test_rate_evidence);
    RUN_TEST(test_streak_accumulates_and_resets);
    RUN_TEST(test_streak_saturates);

    RUN_TEST(test_channel_mismatch_requires_minimum_reads);
    RUN_TEST(test_channel_mismatch_bad_when_mostly_clamped);
    RUN_TEST(test_channel_mismatch_good_when_readings_flow);
    RUN_TEST(test_read_failure_evidence);
    RUN_TEST(test_nominal_voltage_selection);
    RUN_TEST(test_nominal_frequency_selection);
    RUN_TEST(test_voltage_evidence);
    RUN_TEST(test_frequency_evidence);

    RUN_TEST(test_scenario_auto_flip_event_lingers_until_ack);
    RUN_TEST(test_scenario_manual_reverse_mismatch_until_fixed);
    RUN_TEST(test_scenario_overnight_outage_leaves_trace);
    RUN_TEST(test_scenario_flapping_reraises);

    return UNITY_END();
}
