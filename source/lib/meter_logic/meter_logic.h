// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>
#include "channel_types.h"

// Pure measurement and scheduling decisions for the ADE7953 channel pipeline.
//
// Everything here is free-standing: no Arduino, no FreeRTOS, no SPI, no logging,
// no global state. Inputs come in as plain values, decisions go out as plain
// values, and the caller (src/ade7953.cpp) is responsible for all I/O, mutexes
// and for acting on the decisions. That boundary is what lets this logic be
// unit-tested on the host (see test/test_meter_logic) without any hardware.
namespace MeterLogic {

// ============================================================================
// CT polarity (reversed-CT) detector
// ============================================================================
// A net consistent-sign vote accumulator. Each CONDUCTING reading votes +1
// (power flows in the expected direction) or -1 (reversed). The orientation is
// decided once the running net reaches +/- voteThreshold, which makes the
// detector magnitude-independent (a steady -4 W reversed CT is caught like a
// -4 kW one) and immune to transient sign flips (they cancel out).
//
// A non-conducting reading (no load) carries no sign information: it does NOT
// vote and does NOT count toward any bound, so an armed channel with no load
// simply waits - indefinitely if need be. This is deliberate: a CT clipped on a
// circuit that happens to be off at install time must still be checked the moment
// real load first flows. (Starvation is prevented separately, by gating the
// scheduler boost on conduction - see computeChannelWeight - not by disarming.)
//
// maxConductingReads only bounds the pathological case of a channel that DOES
// conduct but whose sign keeps oscillating (never reaching the threshold): after
// that many conducting reads it gives up with no flip.

enum class PolarityAction : uint8_t {
    Continue, // still accumulating - caller does nothing
    Flip,     // threshold of reversed votes reached - caller flips the reverse flag
    Disarm,   // confirmed correct, or gave up on an ambiguous signal - caller stops, no flip
};

struct PolarityState {
    bool armed;               // detection in progress for this channel
    int8_t voteCount;         // running net vote (+ expected, - reversed)
    uint16_t conductingReads; // conducting reads seen since arming (for the bound)
};

struct PolarityConfig {
    int8_t voteThreshold;       // net votes needed to decide (e.g. 5)
    uint16_t maxConductingReads; // give up after this many conducting reads w/o a decision (0 = no bound)
    float minCurrent;            // a reading below this current is offset noise: no vote, no count
};

struct PolarityResult {
    PolarityState state; // updated state to store back
    PolarityAction action;
};

// True if a reading carries usable directional information: a real, non-zero
// power backed by enough current (current >= minCurrent) that the ADE7953's
// offset noise - which shows up as a small, consistent-sign power at essentially
// zero current - cannot masquerade as a genuine (possibly reversed) load. This is
// the one definition of "conducting" used across the pipeline: updatePolarity gates
// the vote on it, and the firmware gates the scheduler's armed boost (_lastConducting,
// feeding computeChannelWeight) on the same call, so the two can never disagree about
// whether a channel is drawing power. NaN power or NaN current -> false.
//
// A reversed load/PV reads NEGATIVE power but with REAL current, so it stays
// conducting - that is what lets a reversed CT both vote (to flip) and earn the boost.
bool isConductingReading(float activePower, float current, float minCurrent);

// One detector step for a single channel reading. Safe to call on every read of
// an armed channel: a non-conducting reading is a no-op. The reading votes only if
// isConductingReading(activePower, current, cfg.minCurrent) - so 0/NaN power, or a
// small offset power at near-zero current, never votes and never counts toward the
// bound. activePower's sign gives the vote direction.
//
// PRECONDITION: feed the RAW signed reading (both power and current). Do NOT
// pre-clamp negative power to zero before calling - a reversed LOAD/PV reads
// negative with real current, and clamping power to 0 first would make the reading
// look non-conducting, so the reversal would never be detected. (The firmware runs
// this detector before shouldClampNegative.) current is the post-validation current
// (an invalid low-current reading is already zeroed upstream, which correctly reads
// as non-conducting here).
PolarityResult updatePolarity(PolarityState state, float activePower, float current, PolarityConfig cfg);

// ============================================================================
// Negative-reading handling
// ============================================================================

// True if a negative active-power reading on this role is physically impossible
// and must be clamped to zero. Load and PV can only consume / generate one way;
// grid, battery and hybrid inverters legitimately go negative (export / charge).
bool shouldClampNegative(float activePower, ChannelRole role);

// ============================================================================
// WDRR weighting
// ============================================================================
// Weighted Deficit Round-Robin sampling weights. Higher weight -> sampled more
// often. Weight = power-share + variability-share + a minimum base (so every
// active channel is eventually serviced), with two priority bumps on top:
//   - grid / battery roles get a multiplier (we want the mains and storage flow
//     sampled more often, especially on 3-phase systems);
//   - a channel actively running polarity detection AND currently conducting (the
//     `conducting` input) gets a flat boost so it resolves quickly. Gating on
//     conduction means an armed channel with no load gets no boost and cannot hog
//     the scheduler while it waits for load (this prevents the no-load starvation),
//     while a reversed load/PV - whose stored power is clamped to 0 but which is
//     genuinely conducting - still earns the boost and resolves in ~1 s.

struct WeightConfig {
    float powerShare;       // contribution from |power| share
    float variability;      // contribution from variability share
    float minBase;          // floor for every active channel (anti-starvation)
    float armedBoost;       // flat add-on while polarity detection is armed AND conducting
    float rolePriorityMult; // multiplier for grid / battery roles (e.g. 2.0)
};

struct ChannelWeightInput {
    bool active;
    float activePower;       // signed; only the magnitude is used (for the power share)
    float variability;       // EMA of |delta power|
    bool armed;              // polarity detection in progress
    ChannelRole role;
    bool conducting = false; // is the channel drawing power *right now*? Drives the
                             // armed boost. Deliberately separate from activePower!=0:
                             // a reversed load/PV is stored clamped to 0 yet is really
                             // conducting, so the caller passes the pre-clamp signal -
                             // otherwise reversed loads would never earn the boost.
};

// True if this role is sampled more often (grid + battery).
bool roleHasSchedulingPriority(ChannelRole role);

// Weight for a single channel given its precomputed shares (each 0..1).
float computeChannelWeight(const ChannelWeightInput& in, float powerShare,
                           float variabilityShare, const WeightConfig& cfg);

// Compute weights for channels [startIndex, count). Indices below startIndex are
// set to 0 (the firmware reads channel 0 separately, outside the rotation).
// NaN power / variability values are excluded from the totals and treated as 0,
// so one bad reading cannot poison every other channel's weight.
void computeWeights(const ChannelWeightInput* in, uint8_t count, uint8_t startIndex,
                    const WeightConfig& cfg, float* outWeights);

// ============================================================================
// WDRR scheduler core
// ============================================================================

// Sentinel for "no channel". Deliberately NOT named INVALID_CHANNEL: the firmware
// defines that as an object-like macro (#define INVALID_CHANNEL 255), which would
// textually rewrite any `MeterLogic::INVALID_CHANNEL` into `MeterLogic::255`. The
// firmware asserts NO_CHANNEL == its own INVALID_CHANNEL so the values stay in sync.
constexpr uint8_t NO_CHANNEL = 255;

// Lowest-index active channel whose gap (now - lastMillis) exceeds maxGap, over
// [startIndex, count). Channels with lastMillis == 0 (never baselined) are
// skipped. Returns NO_CHANNEL if none are starved. Pure lookup - the caller
// updates lastMillis after forcing the read.
uint8_t findStarvedChannel(const uint64_t* lastMillis, const bool* active,
                           uint8_t count, uint8_t startIndex, uint64_t now,
                           uint64_t maxGap);

// Gain phase: add each active channel's weight to its deficit; zero inactive
// channels; guard NaN; clamp symmetrically to +/- deficitBound. Operates on
// [startIndex, count).
void wdrrAccumulate(const float* weights, float* deficits, const bool* active,
                    uint8_t count, uint8_t startIndex, float deficitBound);

// Pick phase: select the highest-deficit active channel over [startIndex, count)
// with a round-robin tie-break starting one past *cursor (so equal-deficit
// channels rotate instead of letting the lowest index monopolise). Decrements
// the winner's deficit by 1 and moves *cursor to it. Returns the winner, or
// NO_CHANNEL if no active channel exists.
uint8_t wdrrPick(float* deficits, const bool* active, uint8_t count,
                 uint8_t startIndex, uint8_t* cursor);

} // namespace MeterLogic
