// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for BootRecoveryLogic. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "boot_recovery_logic.h"

using namespace BootRecoveryLogic;

void setUp(void) {}
void tearDown(void) {}

static Input input(Stage stage, uint8_t unstable, ResetKind kind,
                   bool deliberate = false, bool firmwareMatches = true, bool rollbackAllowed = false) {
    return {stage, unstable, firmwareMatches, kind, deliberate, rollbackAllowed};
}

// Runs `boots` consecutive fault resets from a persisted state, feeding each
// decision back in as the next boot's stored state, like the firmware does.
static Decision loop(Stage stage, uint8_t unstable, uint8_t boots, bool rollbackAllowed = false) {
    Decision d{Action::Boot, stage, unstable};
    for (uint8_t i = 0; i < boots; i++) {
        d = decide(input(d.stage, d.unstableBoots, ResetKind::Fault, false, true, rollbackAllowed));
    }
    return d;
}

// ============================================================================
// Classification
// ============================================================================

void test_fault_is_unstable(void) {
    TEST_ASSERT_TRUE(isUnstableBoot(ResetKind::Fault, false));
    TEST_ASSERT_TRUE(isUnstableBoot(ResetKind::Fault, true)); // a stale flag cannot hide a crash
}

void test_unflagged_software_restart_is_unstable(void) {
    TEST_ASSERT_TRUE(isUnstableBoot(ResetKind::Software, false));
}

void test_deliberate_software_restart_is_stable(void) {
    TEST_ASSERT_FALSE(isUnstableBoot(ResetKind::Software, true));
}

void test_neutral_reset_is_not_counted(void) {
    TEST_ASSERT_FALSE(isUnstableBoot(ResetKind::Neutral, false));
}

// ============================================================================
// Counting
// ============================================================================

void test_two_faults_keep_booting_normal(void) {
    Decision d = loop(Stage::Normal, 0, 2);
    TEST_ASSERT_EQUAL(Action::Boot, d.action);
    TEST_ASSERT_EQUAL(Stage::Normal, d.stage);
    TEST_ASSERT_EQUAL_UINT8(2, d.unstableBoots);
}

void test_power_on_keeps_the_count(void) {
    Decision d = decide(input(Stage::Normal, 2, ResetKind::Neutral));
    TEST_ASSERT_EQUAL(Action::Boot, d.action);
    TEST_ASSERT_EQUAL_UINT8(2, d.unstableBoots);
}

void test_deliberate_restarts_never_escalate(void) {
    Decision d{Action::Boot, Stage::Normal, 0};
    for (int i = 0; i < 20; i++) {
        d = decide(input(d.stage, d.unstableBoots, ResetKind::Software, true));
        TEST_ASSERT_EQUAL(Action::Boot, d.action);
    }
    TEST_ASSERT_EQUAL(Stage::Normal, d.stage);
    TEST_ASSERT_EQUAL_UINT8(0, d.unstableBoots);
}

void test_count_saturates(void) {
    Decision d = decide(input(Stage::Minimal, UINT8_MAX, ResetKind::Fault));
    TEST_ASSERT_EQUAL(Stage::Minimal, d.stage);
    TEST_ASSERT_EQUAL_UINT8(0, d.unstableBoots);
}

// ============================================================================
// Escalation
// ============================================================================

void test_third_fault_on_factory_image_wipes(void) {
    Decision d = loop(Stage::Normal, 0, 3);
    TEST_ASSERT_EQUAL(Action::FactoryReset, d.action);
    TEST_ASSERT_EQUAL(Stage::Wiped, d.stage);
    TEST_ASSERT_EQUAL_UINT8(0, d.unstableBoots);
}

void test_third_fault_rolls_back_when_allowed(void) {
    Decision d = loop(Stage::Normal, 0, 3, true);
    TEST_ASSERT_EQUAL(Action::RollBack, d.action);
    TEST_ASSERT_EQUAL(Stage::Normal, d.stage);
    TEST_ASSERT_EQUAL_UINT8(0, d.unstableBoots);
}

void test_unflagged_software_restarts_escalate(void) {
    // LittleFS failing to mount restarts with a bare ESP.restart()
    Decision d{Action::Boot, Stage::Normal, 0};
    for (int i = 0; i < 3; i++) d = decide(input(d.stage, d.unstableBoots, ResetKind::Software, false));
    TEST_ASSERT_EQUAL(Action::FactoryReset, d.action);
}

void test_full_ladder(void) {
    Decision d = loop(Stage::Normal, 0, 3);
    TEST_ASSERT_EQUAL(Action::FactoryReset, d.action);

    // The ladder's own restart after the wipe is flagged deliberate
    d = decide(input(d.stage, d.unstableBoots, ResetKind::Software, true));
    TEST_ASSERT_EQUAL(Action::Boot, d.action);
    TEST_ASSERT_EQUAL(Stage::Wiped, d.stage);
    TEST_ASSERT_EQUAL_UINT8(0, d.unstableBoots);

    d = loop(d.stage, d.unstableBoots, 3);
    TEST_ASSERT_EQUAL(Action::Boot, d.action);
    TEST_ASSERT_EQUAL(Stage::Recovery, d.stage);

    d = loop(d.stage, d.unstableBoots, 3);
    TEST_ASSERT_EQUAL(Action::Boot, d.action);
    TEST_ASSERT_EQUAL(Stage::Minimal, d.stage);

    d = loop(d.stage, d.unstableBoots, 3);
    TEST_ASSERT_EQUAL(Action::Boot, d.action);
    TEST_ASSERT_EQUAL(Stage::Minimal, d.stage);
}

void test_only_one_wipe_per_firmware(void) {
    // Recovery exit re-enters at Wiped: a repeated failure goes back to Recovery, never wipes again
    Decision d = loop(Stage::Wiped, 0, 3);
    TEST_ASSERT_EQUAL(Action::Boot, d.action);
    TEST_ASSERT_EQUAL(Stage::Recovery, d.stage);
}

// ============================================================================
// Stickiness
// ============================================================================

void test_stage_is_sticky_across_power_cycles(void) {
    Decision d = decide(input(Stage::Recovery, 0, ResetKind::Neutral));
    TEST_ASSERT_EQUAL(Action::Boot, d.action);
    TEST_ASSERT_EQUAL(Stage::Recovery, d.stage);
}

void test_new_firmware_resets_stage_and_count(void) {
    Decision d = decide(input(Stage::Minimal, 2, ResetKind::Software, true, false));
    TEST_ASSERT_EQUAL(Action::Boot, d.action);
    TEST_ASSERT_EQUAL(Stage::Normal, d.stage);
    TEST_ASSERT_EQUAL_UINT8(0, d.unstableBoots);
}

void test_new_firmware_counts_its_own_first_fault(void) {
    Decision d = decide(input(Stage::Minimal, 2, ResetKind::Fault, false, false));
    TEST_ASSERT_EQUAL(Stage::Normal, d.stage);
    TEST_ASSERT_EQUAL_UINT8(1, d.unstableBoots);
}

// ============================================================================
// Persistence format
// ============================================================================

void test_stage_round_trip(void) {
    TEST_ASSERT_EQUAL(Stage::Normal, stageFromByte(0));
    TEST_ASSERT_EQUAL(Stage::Wiped, stageFromByte(1));
    TEST_ASSERT_EQUAL(Stage::Recovery, stageFromByte(2));
    TEST_ASSERT_EQUAL(Stage::Minimal, stageFromByte(3));
}

void test_unknown_stage_byte_is_normal(void) {
    TEST_ASSERT_EQUAL(Stage::Normal, stageFromByte(4));
    TEST_ASSERT_EQUAL(Stage::Normal, stageFromByte(0xFF));
}

void test_stage_names(void) {
    TEST_ASSERT_EQUAL_STRING("normal", stageToString(Stage::Normal));
    TEST_ASSERT_EQUAL_STRING("wiped", stageToString(Stage::Wiped));
    TEST_ASSERT_EQUAL_STRING("recovery", stageToString(Stage::Recovery));
    TEST_ASSERT_EQUAL_STRING("minimal", stageToString(Stage::Minimal));
}

void test_stage_effects(void) {
    TEST_ASSERT_FALSE(meterPassive(Stage::Normal));
    TEST_ASSERT_FALSE(meterPassive(Stage::Wiped));
    TEST_ASSERT_TRUE(meterPassive(Stage::Recovery));
    TEST_ASSERT_TRUE(meterPassive(Stage::Minimal));
    TEST_ASSERT_FALSE(cloudDisabled(Stage::Recovery));
    TEST_ASSERT_TRUE(cloudDisabled(Stage::Minimal));
}

void test_action_names(void) {
    TEST_ASSERT_EQUAL_STRING("boot", actionToString(Action::Boot));
    TEST_ASSERT_EQUAL_STRING("rollback", actionToString(Action::RollBack));
    TEST_ASSERT_EQUAL_STRING("factory_reset", actionToString(Action::FactoryReset));
}

static HistoryEntry entryWithReason(uint8_t reason) {
    HistoryEntry entry = {};
    entry.resetReason = reason;
    return entry;
}

void test_history_appends_until_full(void) {
    HistoryEntry entries[HISTORY_CAPACITY] = {};
    uint8_t count = 0;
    for (uint8_t i = 0; i < HISTORY_CAPACITY; i++) count = pushHistory(entries, count, entryWithReason(i));
    TEST_ASSERT_EQUAL_UINT8(HISTORY_CAPACITY, count);
    TEST_ASSERT_EQUAL_UINT8(0, entries[0].resetReason);
    TEST_ASSERT_EQUAL_UINT8(HISTORY_CAPACITY - 1, entries[HISTORY_CAPACITY - 1].resetReason);
}

void test_history_drops_the_oldest(void) {
    HistoryEntry entries[HISTORY_CAPACITY] = {};
    uint8_t count = 0;
    for (uint8_t i = 0; i < HISTORY_CAPACITY + 2; i++) count = pushHistory(entries, count, entryWithReason(i));
    TEST_ASSERT_EQUAL_UINT8(HISTORY_CAPACITY, count);
    TEST_ASSERT_EQUAL_UINT8(2, entries[0].resetReason);
    TEST_ASSERT_EQUAL_UINT8(HISTORY_CAPACITY + 1, entries[HISTORY_CAPACITY - 1].resetReason);
}

void test_history_tolerates_a_corrupt_count(void) {
    HistoryEntry entries[HISTORY_CAPACITY] = {};
    uint8_t count = pushHistory(entries, 200, entryWithReason(7));
    TEST_ASSERT_EQUAL_UINT8(HISTORY_CAPACITY, count);
    TEST_ASSERT_EQUAL_UINT8(7, entries[HISTORY_CAPACITY - 1].resetReason);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_fault_is_unstable);
    RUN_TEST(test_unflagged_software_restart_is_unstable);
    RUN_TEST(test_deliberate_software_restart_is_stable);
    RUN_TEST(test_neutral_reset_is_not_counted);
    RUN_TEST(test_two_faults_keep_booting_normal);
    RUN_TEST(test_power_on_keeps_the_count);
    RUN_TEST(test_deliberate_restarts_never_escalate);
    RUN_TEST(test_count_saturates);
    RUN_TEST(test_third_fault_on_factory_image_wipes);
    RUN_TEST(test_third_fault_rolls_back_when_allowed);
    RUN_TEST(test_unflagged_software_restarts_escalate);
    RUN_TEST(test_full_ladder);
    RUN_TEST(test_only_one_wipe_per_firmware);
    RUN_TEST(test_stage_is_sticky_across_power_cycles);
    RUN_TEST(test_new_firmware_resets_stage_and_count);
    RUN_TEST(test_new_firmware_counts_its_own_first_fault);
    RUN_TEST(test_stage_round_trip);
    RUN_TEST(test_unknown_stage_byte_is_normal);
    RUN_TEST(test_stage_names);
    RUN_TEST(test_stage_effects);
    RUN_TEST(test_action_names);
    RUN_TEST(test_history_appends_until_full);
    RUN_TEST(test_history_drops_the_oldest);
    RUN_TEST(test_history_tolerates_a_corrupt_count);
    return UNITY_END();
}
