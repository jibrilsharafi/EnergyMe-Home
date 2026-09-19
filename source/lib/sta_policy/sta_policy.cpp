// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "sta_policy.h"

namespace StaPolicy {

namespace {

// Saturating, so a clock that went backwards reads as "no time has passed" rather
// than as a huge elapsed value that would expire the verification and the release
// window at once. Zero is a legitimate timestamp and is deliberately not treated as a
// sentinel; validity is carried by `verify` and `ownDisconnectExpected`.
uint64_t elapsedSince(uint64_t startMs, uint64_t nowMs) {
    if (nowMs <= startMs) return 0;
    return nowMs - startMs;
}

}  // namespace

void init(Context &context) {
    // ACTIVE, so the first step() on a product without a wired interface reports no
    // mode change, and the first one on a wired product reports the boot hold as one.
    context.mode = Mode::ACTIVE;
    context.verify = VerifyResult::NONE;
    context.verifyStartedAtMs = 0;
    context.verifyAttemptStarted = false;
    context.released = false;
    context.nextAttemptNotBeforeMs = 0;
    context.ownDisconnectExpected = false;
    context.releasedAtMs = 0;
}

Mode evaluate(const Context &context, const Inputs &inputs) {
    if (!inputs.wiredManaged) return Mode::ACTIVE;

    // Ahead of everything a wired product can say: the user is waiting on a verdict,
    // and the wire serving is the normal case for a submission, not a reason to skip it.
    if (context.verify == VerifyResult::PENDING) return Mode::VERIFYING;

    if (inputs.bootHold) return Mode::BOOT_HOLD;
    if (inputs.arbitrationWantsSta) return Mode::ACTIVE;

    // Only an ASSOCIATED station is kept. An idle one carries nothing worth protecting,
    // and starting it next to a serving wire is exactly the second address this avoids.
    if (inputs.releaseBlocked && inputs.staAssociated) return Mode::ACTIVE;

    return Mode::STANDBY;
}

bool isStaAllowed(Mode mode) {
    return mode == Mode::ACTIVE || mode == Mode::VERIFYING;
}

StepResult step(Context &context, const Inputs &inputs, uint64_t nowMs) {
    // Before the mode is read, so the step that runs the window out already reports
    // the mode that follows it.
    if (context.verify == VerifyResult::PENDING &&
        elapsedSince(context.verifyStartedAtMs, nowMs) >= STA_POLICY_VERIFY_WINDOW_MS) {
        context.verify = VerifyResult::FAILED;
    }

    StepResult result;
    result.mode = evaluate(context, inputs);
    result.modeChanged = (result.mode != context.mode);
    result.action = Action::NONE;
    context.mode = result.mode;

    // No wired interface: the existing flow owns every start and nothing is ever released.
    if (!inputs.wiredManaged) return result;

    if (!isStaAllowed(result.mode)) {
        // `released` alone is not enough. The disconnect is asynchronous and the driver
        // reconnects by itself, so a station found associated (or an attempt found in
        // flight) after a release is released again, for as long as that holds.
        if (!context.released || inputs.staAssociated || inputs.deadlineArmed) {
            result.action = Action::RELEASE;
        }
        context.released = true;
        return result;
    }

    context.released = false;

    // The stamp can only sit further out than one interval if the clock went backwards.
    // Pull it in, so the worst case stays one interval instead of "until the clock
    // catches up".
    uint64_t latestNotBeforeMs = nowMs + STA_POLICY_ATTEMPT_MIN_INTERVAL_MS;
    if (context.nextAttemptNotBeforeMs > latestNotBeforeMs) context.nextAttemptNotBeforeMs = latestNotBeforeMs;

    if (!inputs.staAssociated && !inputs.deadlineArmed && inputs.credsWorthRetrying &&
        nowMs >= context.nextAttemptNotBeforeMs) {
        result.action = Action::START_ATTEMPT;
        context.nextAttemptNotBeforeMs = latestNotBeforeMs;
    }

    return result;
}

void beginVerification(Context &context, uint64_t nowMs) {
    context.verify = VerifyResult::PENDING;
    context.verifyStartedAtMs = nowMs;
    context.verifyAttemptStarted = false;
}

void onAttemptStarted(Context &context) {
    if (context.verify == VerifyResult::PENDING) context.verifyAttemptStarted = true;
}

void onStaConnected(Context &context) {
    if (context.verify == VerifyResult::PENDING) context.verify = VerifyResult::OK;
}

void onAttemptFailed(Context &context) {
    if (context.verify == VerifyResult::PENDING && context.verifyAttemptStarted) {
        context.verify = VerifyResult::FAILED;
    }
}

void onCredentialsCleared(Context &context) {
    context.verify = VerifyResult::NONE;
    context.verifyAttemptStarted = false;
}

void noteRelease(Context &context, uint64_t nowMs, bool wasAssociated) {
    // An idle release changes nothing: it produces no event of its own, and the echo of
    // an earlier release of an associated station may still be on its way.
    if (!wasAssociated) return;

    context.ownDisconnectExpected = true;
    context.releasedAtMs = nowMs;
}

DisconnectKind classifyDisconnect(Context &context, uint64_t nowMs) {
    if (!context.ownDisconnectExpected) return DisconnectKind::GENUINE;

    // Consumed either way: one release explains one event, and an expectation that
    // outlived its window must not explain a loss that happens later.
    context.ownDisconnectExpected = false;

    if (elapsedSince(context.releasedAtMs, nowMs) >= STA_POLICY_OWN_DISCONNECT_WINDOW_MS) {
        return DisconnectKind::GENUINE;
    }
    return DisconnectKind::OWN_RELEASE;
}

const char *verifyResultName(VerifyResult result) {
    switch (result) {
        case VerifyResult::NONE:    return "none";
        case VerifyResult::PENDING: return "pending";
        case VerifyResult::OK:      return "ok";
        case VerifyResult::FAILED:  return "failed";
    }
    return "none";
}

}  // namespace StaPolicy
