// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure, dependency-free WiFi station policy for products that also have a wired
// interface (Home Pro): one station-side interface at a time.
//
// Holds every rule that decides *whether* the station may run and *what* the WiFi
// task must do about it: the mode (active, held at boot, on standby while the wire
// serves, verifying freshly submitted credentials), the release and start actions,
// the one-shot credential verification verdict, and whether a DISCONNECTED event is
// the echo of the device's own release or a genuine loss.
//
// It knows nothing about Arduino, FreeRTOS, the WiFi driver or the wired driver. The
// caller samples its inputs once per loop, feeds them with a millisecond clock and
// acts on the answer. The glue between "the wire came back" and "the station is
// released exactly once, and restarted the moment it is needed again" is where a
// device gets stranded, so it lives here where it can be host-tested instead of in
// the task loop. On a product without a wired interface every output reduces to
// "station active, nothing to do" and the existing WiFi flow stays in charge.

namespace StaPolicy {

// Timing. Milliseconds.
//
// A submitted credential set gets this long to produce a verdict. One association
// attempt is 10 s (WIFI_CONNECT_TIMEOUT_SECONDS) plus the lease; past that the user is
// looking at a spinner for an answer that is not coming, so the verdict becomes FAILED
// and the station goes back on standby instead of retrying next to a serving wire.
#define STA_POLICY_VERIFY_WINDOW_MS (20UL * 1000UL)

// Minimum spacing between two START_ATTEMPT actions. The start condition is a level,
// not an edge (that is what guarantees an exit from every mode), so a driver that
// refuses the start - no deadline gets armed - would otherwise be asked again on every
// loop iteration.
#define STA_POLICY_ATTEMPT_MIN_INTERVAL_MS (3UL * 1000UL)

// How long after releasing an ASSOCIATED station a DISCONNECTED event is still taken
// to be that release. The driver reports the disconnect asynchronously, normally
// within milliseconds; the window only has to outlast a busy event queue. Past it the
// event is somebody else's and must be handled as a real loss.
#define STA_POLICY_OWN_DISCONNECT_WINDOW_MS (5UL * 1000UL)

enum class Mode : uint8_t {
    ACTIVE,     // The station runs: no wired interface, or the wire is not serving.
    BOOT_HOLD,  // Wired product, boot: the wire has not had its chance yet. Station held.
    STANDBY,    // The wire serves. Station released, credentials kept for a takeover.
    VERIFYING   // Submitted credentials are being tried once, whatever the wire does.
};

enum class VerifyResult : uint8_t {
    NONE,     // Nothing submitted since boot (or the credentials were cleared)
    PENDING,  // Submitted, no verdict yet
    OK,       // The station associated and got an address with them
    FAILED    // A started attempt failed, or the window ran out
};

enum class Action : uint8_t {
    NONE,
    RELEASE,        // Disconnect the station and disarm its deadlines
    START_ATTEMPT   // Start one association attempt from the stored credentials
};

enum class DisconnectKind : uint8_t {
    GENUINE,      // The network dropped the station: count it, warn, feed the loss
    OWN_RELEASE   // Echo of the device's own release: not an error, not a loss
};

// Sampled by the caller once per loop. All of them are levels, never edges.
struct Inputs {
    bool wiredManaged;         // The product has a wired interface. Always false on Home.
    bool bootHold;             // WifiProvisioning::shouldHoldStaAtBoot(), latched off by the caller
    bool arbitrationWantsSta;  // InterfaceArbitration::isStaWanted() (wire down, or release linger)
    bool staAssociated;        // The driver's association bit, NOT "has an address"
    bool releaseBlocked;       // Something must not lose the station now (firmware update in flight)
    bool deadlineArmed;        // A connect or disconnect-grace deadline is running
    bool credsWorthRetrying;   // There is something to associate with
};

struct StepResult {
    Mode mode;
    Action action;
    bool modeChanged;  // True on the one step() where the mode differs from the previous one
};

struct Context {
    Mode mode;  // What the last step() decided. Seeds modeChanged.

    VerifyResult verify;
    uint64_t verifyStartedAtMs;  // Valid while verify == PENDING; zero is a legitimate timestamp

    // A failure only counts against the submitted credentials when an attempt with them
    // was actually started. Submitting while the station is busy cancels what was in
    // flight, and that cancellation (or a start the driver refused) reports a failure too.
    bool verifyAttemptStarted;

    // The last step() found the station not allowed and asked for its release. Guards in
    // the caller read it to drop whatever the driver still delivers for a released
    // station (a late GOT_IP, a forced reconnect).
    bool released;

    uint64_t nextAttemptNotBeforeMs;

    bool ownDisconnectExpected;
    uint64_t releasedAtMs;  // Valid while ownDisconnectExpected
};

void init(Context &context);

// The mode for these inputs. Precedence, first match wins:
//   1. no wired interface            -> ACTIVE (Home: nothing below ever applies)
//   2. verification pending          -> VERIFYING (outranks the boot hold and the wire)
//   3. boot hold                     -> BOOT_HOLD
//   4. arbitration wants the station -> ACTIVE
//   5. release blocked, associated   -> ACTIVE (never drop a station that carries an update)
//   6. otherwise                     -> STANDBY
// A blocked release keeps an associated station; it never starts an idle one.
Mode evaluate(const Context &context, const Inputs &inputs);

// True for the modes in which the station may associate.
bool isStaAllowed(Mode mode);

// Call first in every loop iteration. Expires a pending verification, evaluates the
// mode and returns the single action the caller must perform now.
//   - Not allowed: RELEASE once, and again for as long as the station is found
//     associated or a deadline armed (the driver reconnects by itself, and the
//     disconnect is asynchronous, so one release is not proof it stayed released).
//   - Allowed: START_ATTEMPT whenever the station is idle (not associated, no deadline
//     armed) with credentials worth retrying, rate-limited. A level, so no sequence of
//     events can leave an allowed, idle station waiting for an edge that already passed.
//   - No wired interface: always {ACTIVE, NONE}. The existing flow drives the station.
StepResult step(Context &context, const Inputs &inputs, uint64_t nowMs);

// Credential verification. One verdict per submission, kept in RAM until the next
// submission or until the credentials are cleared.
//
// beginVerification() replaces any previous verdict with PENDING at once, so a reader
// can never see the verdict of the previous submission after a new one was accepted.
void beginVerification(Context &context, uint64_t nowMs);

// An association attempt really started (the driver accepted it). Not for a refused start.
void onAttemptStarted(Context &context);

// The station associated and has an address. Resolves PENDING to OK. Call it BEFORE
// acting on the connection: the mode stops being VERIFYING the moment this returns.
void onStaConnected(Context &context);

// One attempt gave up. Resolves PENDING to FAILED only if onAttemptStarted() was seen
// since beginVerification(); see Context.verifyAttemptStarted.
void onAttemptFailed(Context &context);

// The stored credentials were erased: a verdict about them describes nothing any more.
void onCredentialsCleared(Context &context);

// Own-release bookkeeping. Deliberately blind to the disconnect reason code: event bits
// coalesce and the reason the caller can read is only "the last one seen".
//
// noteRelease(): call right before disconnecting the station on a RELEASE. Only a
// station that was associated will produce a DISCONNECTED event; releasing an idle one
// expects nothing, and does not cancel an expectation still outstanding.
void noteRelease(Context &context, uint64_t nowMs, bool wasAssociated);

// Call on every DISCONNECTED event. OWN_RELEASE at most once per release, and only
// inside the window. Consumes the expectation either way. An attempt started in
// between does not cancel it: the echo of a release can still be queued behind the
// takeover start, and reading it as a loss would count a failure nobody had.
DisconnectKind classifyDisconnect(Context &context, uint64_t nowMs);

// Wire/log name for a verdict: "none", "pending", "ok", "failed". Falls back to "none".
const char *verifyResultName(VerifyResult result);

}  // namespace StaPolicy
