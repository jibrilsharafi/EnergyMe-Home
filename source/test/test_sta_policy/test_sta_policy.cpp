// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for StaPolicy (Home Pro: one station-side interface at a time).
// Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>

#include <cstdint>
#include <cstdio>

#include "sta_policy.h"

using namespace StaPolicy;

static Context ctx;

void setUp(void) { init(ctx); }
void tearDown(void) {}

static const uint64_t VERIFY_WINDOW = STA_POLICY_VERIFY_WINDOW_MS;
static const uint64_t MIN_INTERVAL = STA_POLICY_ATTEMPT_MIN_INTERVAL_MS;
static const uint64_t OWN_WINDOW = STA_POLICY_OWN_DISCONNECT_WINDOW_MS;

namespace {

// A wired product whose wire serves: station idle, credentials stored.
Inputs wireServing() {
    Inputs in{};
    in.wiredManaged = true;
    in.credsWorthRetrying = true;
    return in;
}

// Same product with the wire gone (or still inside the release linger).
Inputs wireLost() {
    Inputs in = wireServing();
    in.arbitrationWantsSta = true;
    return in;
}

// Runs the first step of a cabled boot that is past its hold: the station is released.
void settleIntoStandby(uint64_t nowMs) {
    StepResult r = step(ctx, wireServing(), nowMs);
    TEST_ASSERT_EQUAL(Mode::STANDBY, r.mode);
    TEST_ASSERT_TRUE(ctx.released);
}

}  // namespace

// ============================================================================
// Mode
// ============================================================================

void test_home_is_always_active_and_never_acts(void) {
    // Every combination of the other inputs. On Home the existing WiFi flow owns the
    // station, so an idle station with credentials must NOT draw a START_ATTEMPT here.
    for (unsigned bits = 0; bits < 64u; bits++) {
        Inputs in{};
        in.wiredManaged = false;
        in.bootHold = (bits & 1u) != 0;
        in.arbitrationWantsSta = (bits & 2u) != 0;
        in.staAssociated = (bits & 4u) != 0;
        in.releaseBlocked = (bits & 8u) != 0;
        in.deadlineArmed = (bits & 16u) != 0;
        in.credsWorthRetrying = (bits & 32u) != 0;

        TEST_ASSERT_EQUAL(Mode::ACTIVE, evaluate(ctx, in));

        StepResult r = step(ctx, in, 1000u + (uint64_t)bits * 10000u);
        TEST_ASSERT_EQUAL(Mode::ACTIVE, r.mode);
        TEST_ASSERT_EQUAL(Action::NONE, r.action);
        TEST_ASSERT_FALSE(r.modeChanged);
        TEST_ASSERT_FALSE(ctx.released);
    }

    // Nothing on Home calls beginVerification(), but if it ever did the mode still holds.
    beginVerification(ctx, 0);
    Inputs home{};
    home.credsWorthRetrying = true;
    TEST_ASSERT_EQUAL(Mode::ACTIVE, evaluate(ctx, home));
    TEST_ASSERT_EQUAL(Action::NONE, step(ctx, home, 5000).action);
}

void test_boot_hold_mode_blocks_sta(void) {
    // Before any wire has reported, arbitration wants the station. The hold outranks it.
    Inputs in = wireLost();
    in.bootHold = true;

    TEST_ASSERT_EQUAL(Mode::BOOT_HOLD, evaluate(ctx, in));
    TEST_ASSERT_FALSE(isStaAllowed(Mode::BOOT_HOLD));

    for (uint64_t t = 0; t <= 10000; t += 1000) {
        StepResult r = step(ctx, in, t);
        TEST_ASSERT_EQUAL(Mode::BOOT_HOLD, r.mode);
        TEST_ASSERT_TRUE(r.action != Action::START_ATTEMPT);
    }

    // It also outranks a blocked release: nothing can be updating this early.
    in.releaseBlocked = true;
    in.staAssociated = true;
    TEST_ASSERT_EQUAL(Mode::BOOT_HOLD, evaluate(ctx, in));

    // Hold over with no wire: the station starts on that very step.
    Inputs after = wireLost();
    StepResult r = step(ctx, after, 10001);
    TEST_ASSERT_EQUAL(Mode::ACTIVE, r.mode);
    TEST_ASSERT_EQUAL(Action::START_ATTEMPT, r.action);
}

void test_standby_when_arbitration_does_not_want_sta(void) {
    Inputs in = wireServing();
    TEST_ASSERT_EQUAL(Mode::STANDBY, evaluate(ctx, in));
    TEST_ASSERT_FALSE(isStaAllowed(Mode::STANDBY));

    // Stored credentials are not exercised while the wire serves.
    for (uint64_t t = 1000; t <= 60000; t += 1000) {
        TEST_ASSERT_TRUE(step(ctx, in, t).action != Action::START_ATTEMPT);
    }
}

void test_active_when_arbitration_wants_sta(void) {
    Inputs in = wireLost();
    TEST_ASSERT_EQUAL(Mode::ACTIVE, evaluate(ctx, in));
    TEST_ASSERT_TRUE(isStaAllowed(Mode::ACTIVE));
    TEST_ASSERT_TRUE(isStaAllowed(Mode::VERIFYING));
}

void test_release_blocked_keeps_associated_sta_active(void) {
    // Firmware upload running over WiFi when the wire's hold-down and linger run out.
    Inputs in = wireServing();
    in.staAssociated = true;
    in.releaseBlocked = true;

    TEST_ASSERT_EQUAL(Mode::ACTIVE, evaluate(ctx, in));
    for (uint64_t t = 1000; t <= 30000; t += 1000) {
        StepResult r = step(ctx, in, t);
        TEST_ASSERT_EQUAL(Mode::ACTIVE, r.mode);
        TEST_ASSERT_EQUAL(Action::NONE, r.action);
    }

    // The upload ends: the deferred release happens now.
    in.releaseBlocked = false;
    StepResult r = step(ctx, in, 31000);
    TEST_ASSERT_EQUAL(Mode::STANDBY, r.mode);
    TEST_ASSERT_EQUAL(Action::RELEASE, r.action);
}

void test_release_blocked_does_not_start_idle_sta(void) {
    // An update over the wire must not drag an idle station up next to it.
    Inputs in = wireServing();
    in.releaseBlocked = true;

    TEST_ASSERT_EQUAL(Mode::STANDBY, evaluate(ctx, in));
    for (uint64_t t = 1000; t <= 30000; t += 1000) {
        StepResult r = step(ctx, in, t);
        TEST_ASSERT_EQUAL(Mode::STANDBY, r.mode);
        TEST_ASSERT_TRUE(r.action != Action::START_ATTEMPT);
    }
}

// ============================================================================
// Credential verification
// ============================================================================

void test_verification_makes_standby_verifying(void) {
    settleIntoStandby(1000);

    beginVerification(ctx, 2000);
    TEST_ASSERT_EQUAL(VerifyResult::PENDING, ctx.verify);
    TEST_ASSERT_EQUAL(Mode::VERIFYING, evaluate(ctx, wireServing()));

    // The caller started the attempt itself right after the submission. On standby an
    // armed deadline means RELEASE; while verifying it must be left alone.
    Inputs in = wireServing();
    in.deadlineArmed = true;
    StepResult r = step(ctx, in, 2001);
    TEST_ASSERT_EQUAL(Mode::VERIFYING, r.mode);
    TEST_ASSERT_TRUE(r.modeChanged);
    TEST_ASSERT_EQUAL(Action::NONE, r.action);
    TEST_ASSERT_FALSE(ctx.released);
}

void test_verification_overrides_boot_hold(void) {
    // Credentials submitted over the wire inside the first seconds of a boot.
    Inputs in = wireServing();
    in.bootHold = true;
    TEST_ASSERT_EQUAL(Mode::BOOT_HOLD, evaluate(ctx, in));

    beginVerification(ctx, 3000);
    TEST_ASSERT_EQUAL(Mode::VERIFYING, evaluate(ctx, in));
}

void test_verification_ok_returns_to_standby_with_result(void) {
    settleIntoStandby(1000);
    beginVerification(ctx, 2000);
    onAttemptStarted(ctx);

    Inputs in = wireServing();
    in.deadlineArmed = true;
    TEST_ASSERT_EQUAL(Mode::VERIFYING, step(ctx, in, 2001).mode);

    onStaConnected(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::OK, ctx.verify);

    // Verified once, then released: the wire never stopped serving.
    in.deadlineArmed = false;
    in.staAssociated = true;
    StepResult r = step(ctx, in, 6000);
    TEST_ASSERT_EQUAL(Mode::STANDBY, r.mode);
    TEST_ASSERT_TRUE(r.modeChanged);
    TEST_ASSERT_EQUAL(Action::RELEASE, r.action);
    TEST_ASSERT_EQUAL(VerifyResult::OK, ctx.verify);
}

void test_mode_after_on_sta_connected_is_not_verifying(void) {
    // The verdict flips inside onStaConnected(), with no step() in between. A caller
    // that wants to know "was this connection a verification" has to latch that BEFORE
    // feeding the connection; reading the mode afterwards is already too late.
    beginVerification(ctx, 2000);
    onAttemptStarted(ctx);

    Inputs in = wireServing();
    in.staAssociated = true;
    TEST_ASSERT_EQUAL(Mode::VERIFYING, evaluate(ctx, in));

    onStaConnected(ctx);
    TEST_ASSERT_EQUAL(Mode::STANDBY, evaluate(ctx, in));
}

void test_verification_fails_on_first_started_attempt_failure(void) {
    settleIntoStandby(1000);
    beginVerification(ctx, 2000);
    onAttemptStarted(ctx);

    Inputs in = wireServing();
    in.deadlineArmed = true;
    TEST_ASSERT_EQUAL(Mode::VERIFYING, step(ctx, in, 2001).mode);

    // One attempt, one verdict: nothing keeps retrying next to a serving wire.
    onAttemptFailed(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::FAILED, ctx.verify);

    StepResult r = step(ctx, wireServing(), 12000);
    TEST_ASSERT_EQUAL(Mode::STANDBY, r.mode);
    TEST_ASSERT_EQUAL(Action::RELEASE, r.action);
    TEST_ASSERT_EQUAL(Action::NONE, step(ctx, wireServing(), 12100).action);
    TEST_ASSERT_EQUAL(VerifyResult::FAILED, ctx.verify);
}

void test_refusal_without_started_attempt_keeps_pending(void) {
    settleIntoStandby(1000);
    beginVerification(ctx, 2000);

    // The driver refused the start (or the submission cancelled an attempt that was in
    // flight): a failure is reported, but not one of the submitted credentials.
    onAttemptFailed(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::PENDING, ctx.verify);

    // No deadline was armed, so the level start asks again.
    StepResult r = step(ctx, wireServing(), 2001);
    TEST_ASSERT_EQUAL(Mode::VERIFYING, r.mode);
    TEST_ASSERT_EQUAL(Action::START_ATTEMPT, r.action);

    onAttemptStarted(ctx);
    onAttemptFailed(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::FAILED, ctx.verify);
}

void test_verification_times_out_to_failed(void) {
    settleIntoStandby(1000);
    beginVerification(ctx, 2000);
    onAttemptStarted(ctx);

    Inputs in = wireServing();
    in.deadlineArmed = true;

    StepResult r = step(ctx, in, 2000 + VERIFY_WINDOW - 1);
    TEST_ASSERT_EQUAL(Mode::VERIFYING, r.mode);
    TEST_ASSERT_EQUAL(VerifyResult::PENDING, ctx.verify);

    // The same step that runs the window out already releases the attempt in flight.
    r = step(ctx, in, 2000 + VERIFY_WINDOW);
    TEST_ASSERT_EQUAL(VerifyResult::FAILED, ctx.verify);
    TEST_ASSERT_EQUAL(Mode::STANDBY, r.mode);
    TEST_ASSERT_TRUE(r.modeChanged);
    TEST_ASSERT_EQUAL(Action::RELEASE, r.action);
}

void test_result_kept_until_next_submission(void) {
    settleIntoStandby(1000);
    beginVerification(ctx, 2000);
    onAttemptStarted(ctx);
    onStaConnected(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::OK, ctx.verify);

    // An hour of steps across cable pulls and returns: only a submission replaces it.
    uint64_t t = 10000;
    for (int i = 0; i < 60; i++) {
        step(ctx, (i % 2 == 0) ? wireLost() : wireServing(), t);
        TEST_ASSERT_EQUAL(VerifyResult::OK, ctx.verify);
        t += 60000;
    }

    beginVerification(ctx, t);
    TEST_ASSERT_EQUAL(VerifyResult::PENDING, ctx.verify);
}

void test_second_submission_replaces_result_immediately(void) {
    beginVerification(ctx, 2000);
    onAttemptStarted(ctx);
    onAttemptFailed(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::FAILED, ctx.verify);

    // No step() in between: a reader polling right after the POST never sees the old verdict.
    beginVerification(ctx, 10000);
    TEST_ASSERT_EQUAL(VerifyResult::PENDING, ctx.verify);

    // The first submission's started attempt does not vouch for the second one.
    onAttemptFailed(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::PENDING, ctx.verify);

    // And the window counts from the second submission.
    Inputs in = wireServing();
    in.deadlineArmed = true;
    step(ctx, in, 2000 + VERIFY_WINDOW);
    TEST_ASSERT_EQUAL(VerifyResult::PENDING, ctx.verify);
    step(ctx, in, 10000 + VERIFY_WINDOW);
    TEST_ASSERT_EQUAL(VerifyResult::FAILED, ctx.verify);
}

void test_begin_verification_while_sta_up_holds_until_resolved(void) {
    // Cable just returned: the station is still up, inside the release linger.
    Inputs in = wireLost();
    in.staAssociated = true;
    TEST_ASSERT_EQUAL(Action::NONE, step(ctx, in, 1000).action);

    beginVerification(ctx, 2000);

    // The linger runs out while the verdict is open. The station must not be pulled
    // from under the verification: the user would get "failed" for good credentials.
    in.arbitrationWantsSta = false;
    for (uint64_t t = 3000; t < 2000 + VERIFY_WINDOW; t += 1000) {
        StepResult r = step(ctx, in, t);
        TEST_ASSERT_EQUAL(Mode::VERIFYING, r.mode);
        TEST_ASSERT_EQUAL(Action::NONE, r.action);
    }

    onAttemptStarted(ctx);
    onStaConnected(ctx);
    StepResult r = step(ctx, in, 2000 + VERIFY_WINDOW - 1);
    TEST_ASSERT_EQUAL(VerifyResult::OK, ctx.verify);
    TEST_ASSERT_EQUAL(Mode::STANDBY, r.mode);
    TEST_ASSERT_EQUAL(Action::RELEASE, r.action);
}

void test_events_without_pending_do_not_change_result(void) {
    // Ordinary takeovers on a device nobody submitted credentials to.
    onAttemptStarted(ctx);
    onAttemptFailed(ctx);
    onStaConnected(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::NONE, ctx.verify);

    beginVerification(ctx, 2000);
    onAttemptStarted(ctx);
    onStaConnected(ctx);
    onAttemptStarted(ctx);
    onAttemptFailed(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::OK, ctx.verify);

    beginVerification(ctx, 30000);
    onAttemptStarted(ctx);
    onAttemptFailed(ctx);
    onStaConnected(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::FAILED, ctx.verify);
}

void test_wire_lost_during_verification_is_active_and_still_resolves(void) {
    settleIntoStandby(1000);
    beginVerification(ctx, 2000);
    onAttemptStarted(ctx);

    // Cable pulled while the verdict is open. The pending verification still names the
    // mode, but the station stays allowed and is never released.
    Inputs in = wireLost();
    in.deadlineArmed = true;
    StepResult r = step(ctx, in, 3000);
    TEST_ASSERT_EQUAL(Mode::VERIFYING, r.mode);
    TEST_ASSERT_TRUE(isStaAllowed(r.mode));
    TEST_ASSERT_EQUAL(Action::NONE, r.action);

    onStaConnected(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::OK, ctx.verify);

    // Resolved, and the station is now the takeover: ACTIVE, kept.
    in.deadlineArmed = false;
    in.staAssociated = true;
    r = step(ctx, in, 4000);
    TEST_ASSERT_EQUAL(Mode::ACTIVE, r.mode);
    TEST_ASSERT_EQUAL(Action::NONE, r.action);
    TEST_ASSERT_FALSE(ctx.released);

    // Same pull with credentials that fail: the verdict is FAILED, and the device keeps
    // trying them because they are all it has.
    init(ctx);
    settleIntoStandby(1000);
    beginVerification(ctx, 2000);
    onAttemptStarted(ctx);
    onAttemptFailed(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::FAILED, ctx.verify);

    r = step(ctx, wireLost(), 12000);
    TEST_ASSERT_EQUAL(Mode::ACTIVE, r.mode);
    TEST_ASSERT_EQUAL(Action::START_ATTEMPT, r.action);
}

void test_credentials_cleared_resets_verification(void) {
    beginVerification(ctx, 2000);
    onAttemptStarted(ctx);

    onCredentialsCleared(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::NONE, ctx.verify);
    TEST_ASSERT_EQUAL(Mode::STANDBY, evaluate(ctx, wireServing()));

    // The cancelled attempt reporting in must not produce a verdict about nothing.
    onAttemptFailed(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::NONE, ctx.verify);

    beginVerification(ctx, 5000);
    onAttemptStarted(ctx);
    onStaConnected(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::OK, ctx.verify);
    onCredentialsCleared(ctx);
    TEST_ASSERT_EQUAL(VerifyResult::NONE, ctx.verify);
}

void test_clock_backwards_does_not_expire_verification(void) {
    beginVerification(ctx, 100000);

    Inputs in = wireServing();
    in.deadlineArmed = true;

    step(ctx, in, 50000);
    TEST_ASSERT_EQUAL(VerifyResult::PENDING, ctx.verify);
    step(ctx, in, 0);
    TEST_ASSERT_EQUAL(VerifyResult::PENDING, ctx.verify);

    step(ctx, in, 100000 + VERIFY_WINDOW - 1);
    TEST_ASSERT_EQUAL(VerifyResult::PENDING, ctx.verify);
    step(ctx, in, 100000 + VERIFY_WINDOW);
    TEST_ASSERT_EQUAL(VerifyResult::FAILED, ctx.verify);
}

void test_verify_result_names(void) {
    TEST_ASSERT_EQUAL_STRING("none", verifyResultName(VerifyResult::NONE));
    TEST_ASSERT_EQUAL_STRING("pending", verifyResultName(VerifyResult::PENDING));
    TEST_ASSERT_EQUAL_STRING("ok", verifyResultName(VerifyResult::OK));
    TEST_ASSERT_EQUAL_STRING("failed", verifyResultName(VerifyResult::FAILED));
    TEST_ASSERT_EQUAL_STRING("none", verifyResultName((VerifyResult)200));
}

// ============================================================================
// step(): release and start actions
// ============================================================================

void test_step_first_disallowed_emits_release_once(void) {
    StepResult r = step(ctx, wireServing(), 1000);
    TEST_ASSERT_EQUAL(Action::RELEASE, r.action);
    TEST_ASSERT_TRUE(ctx.released);

    for (uint64_t t = 2000; t <= 60000; t += 1000) {
        TEST_ASSERT_EQUAL(Action::NONE, step(ctx, wireServing(), t).action);
    }
}

void test_step_associated_while_released_emits_release_again(void) {
    step(ctx, wireServing(), 1000);
    TEST_ASSERT_EQUAL(Action::NONE, step(ctx, wireServing(), 2000).action);

    // The driver's own reconnect won a race, or the first disconnect has not landed yet.
    Inputs in = wireServing();
    in.staAssociated = true;
    TEST_ASSERT_EQUAL(Action::RELEASE, step(ctx, in, 3000).action);
    TEST_ASSERT_EQUAL(Action::RELEASE, step(ctx, in, 3100).action);

    TEST_ASSERT_EQUAL(Action::NONE, step(ctx, wireServing(), 3200).action);
}

void test_step_deadline_armed_while_disallowed_emits_release(void) {
    step(ctx, wireServing(), 1000);

    // Something armed an attempt or a disconnect grace behind the policy's back.
    Inputs in = wireServing();
    in.deadlineArmed = true;
    TEST_ASSERT_EQUAL(Action::RELEASE, step(ctx, in, 2000).action);

    TEST_ASSERT_EQUAL(Action::NONE, step(ctx, wireServing(), 2100).action);
}

void test_step_release_then_allowed_emits_exactly_one_start(void) {
    TEST_ASSERT_EQUAL(Action::RELEASE, step(ctx, wireServing(), 1000).action);

    // Cable pulled. A level, so it does not matter how long ago the release was.
    StepResult r = step(ctx, wireLost(), 600000);
    TEST_ASSERT_EQUAL(Mode::ACTIVE, r.mode);
    TEST_ASSERT_EQUAL(Action::START_ATTEMPT, r.action);
    TEST_ASSERT_FALSE(ctx.released);

    // The caller armed the connect deadline: the attempt in flight is left alone, also
    // well past the rate limit.
    Inputs in = wireLost();
    in.deadlineArmed = true;
    for (uint64_t t = 600100; t <= 610000; t += 100) {
        TEST_ASSERT_EQUAL(Action::NONE, step(ctx, in, t).action);
    }
}

void test_step_allowed_with_deadline_armed_emits_none(void) {
    Inputs in = wireLost();
    in.deadlineArmed = true;
    for (uint64_t t = 1000; t <= 30000; t += 1000) {
        TEST_ASSERT_EQUAL(Action::NONE, step(ctx, in, t).action);
    }
}

void test_step_allowed_while_associated_emits_none(void) {
    Inputs in = wireLost();
    in.staAssociated = true;
    for (uint64_t t = 1000; t <= 30000; t += 1000) {
        TEST_ASSERT_EQUAL(Action::NONE, step(ctx, in, t).action);
    }
}

void test_step_allowed_without_credentials_emits_none(void) {
    // Nothing to associate with: the AP is this device's way out, not the station.
    Inputs in = wireLost();
    in.credsWorthRetrying = false;
    for (uint64_t t = 1000; t <= 30000; t += 1000) {
        TEST_ASSERT_EQUAL(Action::NONE, step(ctx, in, t).action);
    }

    in.credsWorthRetrying = true;
    TEST_ASSERT_EQUAL(Action::START_ATTEMPT, step(ctx, in, 31000).action);
}

void test_step_start_is_rate_limited(void) {
    // The driver keeps refusing the start, so no deadline is ever armed and the level
    // condition stays true on every loop iteration.
    const uint64_t t0 = 50000;
    TEST_ASSERT_EQUAL(Action::START_ATTEMPT, step(ctx, wireLost(), t0).action);

    for (uint64_t t = t0 + 1; t < t0 + MIN_INTERVAL; t += 100) {
        TEST_ASSERT_EQUAL(Action::NONE, step(ctx, wireLost(), t).action);
    }
    TEST_ASSERT_EQUAL(Action::NONE, step(ctx, wireLost(), t0 + MIN_INTERVAL - 1).action);

    TEST_ASSERT_EQUAL(Action::START_ATTEMPT, step(ctx, wireLost(), t0 + MIN_INTERVAL).action);
    TEST_ASSERT_EQUAL(Action::NONE, step(ctx, wireLost(), t0 + MIN_INTERVAL + 1).action);
    TEST_ASSERT_EQUAL(Action::START_ATTEMPT, step(ctx, wireLost(), t0 + 2 * MIN_INTERVAL).action);
}

void test_step_no_mode_without_exit(void) {
    // Wire lost, credentials present, station idle. Whatever the context looked like
    // before - any prior mode, either released flag, a start stamped a moment ago, even a
    // stamp left far in the future by a clock that went backwards - a start must come
    // within one interval. The fields are forced directly so that combinations no
    // current event sequence produces are covered too.
    const Mode priorModes[] = {Mode::ACTIVE, Mode::BOOT_HOLD, Mode::STANDBY, Mode::VERIFYING};
    const bool releasedFlags[] = {false, true};
    const uint64_t t0 = 100000;
    const uint64_t stamps[] = {0, t0 + MIN_INTERVAL, t0 + 3600000};

    for (Mode prior : priorModes) {
        for (bool released : releasedFlags) {
            for (uint64_t stamp : stamps) {
                init(ctx);
                ctx.mode = prior;
                ctx.released = released;
                ctx.nextAttemptNotBeforeMs = stamp;
                if (prior == Mode::VERIFYING) beginVerification(ctx, t0);

                bool started = false;
                for (uint64_t t = t0; t <= t0 + MIN_INTERVAL && !started; t += 500) {
                    StepResult r = step(ctx, wireLost(), t);
                    TEST_ASSERT_TRUE(isStaAllowed(r.mode));
                    TEST_ASSERT_TRUE(r.action != Action::RELEASE);
                    started = (r.action == Action::START_ATTEMPT);
                }

                char message[96];
                snprintf(message, sizeof(message), "no start: prior mode %u, released %u, stamp %llu",
                         (unsigned)prior, (unsigned)released, (unsigned long long)stamp);
                TEST_ASSERT_TRUE_MESSAGE(started, message);
                TEST_ASSERT_FALSE(ctx.released);
            }
        }
    }
}

void test_step_mode_changed_flag_fires_once_per_edge(void) {
    Inputs hold = wireLost();
    hold.bootHold = true;

    TEST_ASSERT_TRUE(step(ctx, hold, 1000).modeChanged);           // ACTIVE (init) -> BOOT_HOLD
    TEST_ASSERT_FALSE(step(ctx, hold, 2000).modeChanged);

    TEST_ASSERT_TRUE(step(ctx, wireServing(), 3000).modeChanged);  // -> STANDBY
    TEST_ASSERT_FALSE(step(ctx, wireServing(), 4000).modeChanged);

    beginVerification(ctx, 4500);
    TEST_ASSERT_TRUE(step(ctx, wireServing(), 5000).modeChanged);  // -> VERIFYING
    TEST_ASSERT_FALSE(step(ctx, wireServing(), 6000).modeChanged);

    onStaConnected(ctx);
    TEST_ASSERT_TRUE(step(ctx, wireServing(), 7000).modeChanged);  // -> STANDBY
    TEST_ASSERT_FALSE(step(ctx, wireServing(), 8000).modeChanged);

    TEST_ASSERT_TRUE(step(ctx, wireLost(), 9000).modeChanged);     // -> ACTIVE
    TEST_ASSERT_FALSE(step(ctx, wireLost(), 10000).modeChanged);
}

// ============================================================================
// Disconnect classification
// ============================================================================

void test_own_release_disconnect_is_classified_once(void) {
    noteRelease(ctx, 1000, true);
    TEST_ASSERT_EQUAL(DisconnectKind::OWN_RELEASE, classifyDisconnect(ctx, 1100));

    // One release explains one event. The next drop is the network's.
    TEST_ASSERT_EQUAL(DisconnectKind::GENUINE, classifyDisconnect(ctx, 1200));
}

void test_own_release_window_expires_to_genuine(void) {
    noteRelease(ctx, 1000, true);
    TEST_ASSERT_EQUAL(DisconnectKind::OWN_RELEASE, classifyDisconnect(ctx, 1000 + OWN_WINDOW - 1));

    noteRelease(ctx, 1000, true);
    TEST_ASSERT_EQUAL(DisconnectKind::GENUINE, classifyDisconnect(ctx, 1000 + OWN_WINDOW));

    // A clock that went backwards reads as "no time has passed", not as expired.
    noteRelease(ctx, 100000, true);
    TEST_ASSERT_EQUAL(DisconnectKind::OWN_RELEASE, classifyDisconnect(ctx, 50000));

    // A device that never released anything has nothing to excuse.
    init(ctx);
    TEST_ASSERT_EQUAL(DisconnectKind::GENUINE, classifyDisconnect(ctx, 0));
}

void test_release_of_idle_sta_expects_no_disconnect(void) {
    // Cabled boot: the release of a station that never associated produces no event, so
    // a disconnect shortly after a later takeover must not be excused by it.
    noteRelease(ctx, 1000, false);
    TEST_ASSERT_EQUAL(DisconnectKind::GENUINE, classifyDisconnect(ctx, 1001));

    // An idle re-release (deadline found armed) does not cancel the echo still expected
    // from the release of the associated station just before it.
    noteRelease(ctx, 2000, true);
    noteRelease(ctx, 2050, false);
    TEST_ASSERT_EQUAL(DisconnectKind::OWN_RELEASE, classifyDisconnect(ctx, 2100));
}

void test_stale_release_disconnect_after_takeover_start_is_not_genuine(void) {
    // Released on a cable return, cable pulled again before the driver delivered the
    // DISCONNECTED event of that release.
    Inputs in = wireServing();
    in.staAssociated = true;
    TEST_ASSERT_EQUAL(Action::RELEASE, step(ctx, in, 1000).action);
    noteRelease(ctx, 1000, true);

    TEST_ASSERT_EQUAL(Action::START_ATTEMPT, step(ctx, wireLost(), 1050).action);
    onAttemptStarted(ctx);

    // The stale event lands behind the takeover start. Read as a loss it would count a
    // connection error, warn, and arm a disconnect grace against a healthy attempt.
    TEST_ASSERT_EQUAL(DisconnectKind::OWN_RELEASE, classifyDisconnect(ctx, 1100));

    // The takeover attempt's own failure afterwards is the network's.
    TEST_ASSERT_EQUAL(DisconnectKind::GENUINE, classifyDisconnect(ctx, 4000));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_home_is_always_active_and_never_acts);
    RUN_TEST(test_boot_hold_mode_blocks_sta);
    RUN_TEST(test_standby_when_arbitration_does_not_want_sta);
    RUN_TEST(test_active_when_arbitration_wants_sta);
    RUN_TEST(test_release_blocked_keeps_associated_sta_active);
    RUN_TEST(test_release_blocked_does_not_start_idle_sta);

    RUN_TEST(test_verification_makes_standby_verifying);
    RUN_TEST(test_verification_overrides_boot_hold);
    RUN_TEST(test_verification_ok_returns_to_standby_with_result);
    RUN_TEST(test_mode_after_on_sta_connected_is_not_verifying);
    RUN_TEST(test_verification_fails_on_first_started_attempt_failure);
    RUN_TEST(test_refusal_without_started_attempt_keeps_pending);
    RUN_TEST(test_verification_times_out_to_failed);
    RUN_TEST(test_result_kept_until_next_submission);
    RUN_TEST(test_second_submission_replaces_result_immediately);
    RUN_TEST(test_begin_verification_while_sta_up_holds_until_resolved);
    RUN_TEST(test_events_without_pending_do_not_change_result);
    RUN_TEST(test_wire_lost_during_verification_is_active_and_still_resolves);
    RUN_TEST(test_credentials_cleared_resets_verification);
    RUN_TEST(test_clock_backwards_does_not_expire_verification);
    RUN_TEST(test_verify_result_names);

    RUN_TEST(test_step_first_disallowed_emits_release_once);
    RUN_TEST(test_step_associated_while_released_emits_release_again);
    RUN_TEST(test_step_deadline_armed_while_disallowed_emits_release);
    RUN_TEST(test_step_release_then_allowed_emits_exactly_one_start);
    RUN_TEST(test_step_allowed_with_deadline_armed_emits_none);
    RUN_TEST(test_step_allowed_while_associated_emits_none);
    RUN_TEST(test_step_allowed_without_credentials_emits_none);
    RUN_TEST(test_step_start_is_rate_limited);
    RUN_TEST(test_step_no_mode_without_exit);
    RUN_TEST(test_step_mode_changed_flag_fires_once_per_edge);

    RUN_TEST(test_own_release_disconnect_is_classified_once);
    RUN_TEST(test_own_release_window_expires_to_genuine);
    RUN_TEST(test_release_of_idle_sta_expects_no_disconnect);
    RUN_TEST(test_stale_release_disconnect_after_takeover_start_is_not_genuine);

    return UNITY_END();
}
