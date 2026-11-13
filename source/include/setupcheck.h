// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Setup state and the setup phase check (openspec change add-phase-check).
//
// The phase check runs only inside a check window: while the device has never completed
// setup, and for SETUP_RERUN_WINDOW_MS after "Run setup again". Outside it there is no
// evidence, no evaluation and no RAM in use. It advises and never changes the
// configuration on its own. The decisions are in lib/phase_check (host-tested); this module
// feeds it from the meter task and serves it to the REST API.

#define PREFERENCES_SETUP_DONE_KEY "setup_done"
#define SETUP_RERUN_WINDOW_MS (2ULL * 60ULL * 60ULL * 1000ULL)
#define SETUP_CHECK_MINUTE_MS 60000ULL

namespace SetupCheck {

// Boot: `firstBootThisBoot` is true on the very first boot of a factory-fresh (or factory
// reset) device. A device that booted before this firmware counts as set up, so an
// upgrade never shows the setup page, never holds the cloud and never resets energy.
void begin(bool firstBootThisBoot);

bool isSetupDone();
// Completes (or skips) setup: closes the window and, on the first setup only, resets the
// energy counters so the install starts from zero. False (and nothing changed) when the
// completion cannot be stored or the meter is passive (recovery boot); `firstSetup` tells
// whether this was the first setup.
bool completeSetup(bool &firstSetup);
// "Run setup again": opens the check window for SETUP_RERUN_WINDOW_MS with fresh evidence
// (RAM only). False on a recovery boot, where the meter is passive.
bool startRerun();
bool isWindowOpen();

// Meter task hooks.
// One valid reading of a channel: raw (pre-reverse, pre-rotation, pre-clamp) powers against
// V1, the time since its previous reading and its RMS current.
void onReading(uint8_t channelIndex, float rawActivePower, float rawReactivePower, uint64_t deltaMillis, float currentRms);
// Called on every meter cycle: closes minutes, opens and closes the window, re-evaluates
// after configuration writes.
void onTick(uint64_t nowMillis);
// True while the CT-reversal sign vote must not run: the window is open on a three-phase
// device (a flip would hide a wrong phase as a 60-degree error).
bool holdPolarityFlips();

// Any configuration write to a channel (or the voltage phase, channelIndex = 0xFF for all).
void onConfigWrite(uint8_t channelIndex);

// REST.
void toJson(JsonDocument &doc);
enum class ApplyResult : uint8_t { Applied, NoChange, Stale, NotOffered, NotFound, WindowClosed, Failed };
ApplyResult apply(uint8_t lowestChannel, uint32_t generation, const uint8_t channels[3], const uint8_t phases[3], const bool reverse[3]);
const char *applyResultMessage(ApplyResult result);

} // namespace SetupCheck
