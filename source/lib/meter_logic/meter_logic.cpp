// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "meter_logic.h"

#include <cmath>

namespace MeterLogic {

// ----------------------------------------------------------------------------
// CT polarity detector
// ----------------------------------------------------------------------------
PolarityResult updatePolarity(PolarityState state, float activePower, PolarityConfig cfg) {
    PolarityResult result{state, PolarityAction::Continue};

    // Not armed: nothing to do. Defensive - callers should only invoke while armed.
    if (!state.armed) return result;

    // A non-positive threshold is a misconfiguration; treat it as a safe no-op
    // rather than letting the sign comparisons below decide on the very first read.
    if (cfg.voteThreshold <= 0) return result;

    // Only conducting samples carry sign information. A non-conducting reading
    // (no load, or a reading the validation pipeline zeroed) is a pure no-op: the
    // channel stays armed and waits, without ever counting toward the bound. This
    // is what lets a CT clipped on a circuit that is off at install time still be
    // checked once load eventually flows.
    bool conducting = (activePower != 0.0f && !std::isnan(activePower));
    if (!conducting) return result;

    result.state.conductingReads++;
    result.state.voteCount += (activePower < 0.0f) ? -1 : 1;

    if (result.state.voteCount <= -cfg.voteThreshold) {
        result.action = PolarityAction::Flip;
        result.state.armed = false;
        result.state.voteCount = 0;
        result.state.conductingReads = 0;
        return result;
    }
    if (result.state.voteCount >= cfg.voteThreshold) {
        result.action = PolarityAction::Disarm;
        result.state.armed = false;
        result.state.voteCount = 0;
        result.state.conductingReads = 0;
        return result;
    }

    // Bound only the pathological case: a channel that keeps conducting but whose
    // sign oscillates without ever reaching the threshold. Give up with no flip -
    // an ambiguous signal cannot be oriented automatically.
    if (cfg.maxConductingReads > 0 && result.state.conductingReads >= cfg.maxConductingReads) {
        result.action = PolarityAction::Disarm;
        result.state.armed = false;
        result.state.voteCount = 0;
        result.state.conductingReads = 0;
    }

    return result;
}

// ----------------------------------------------------------------------------
// Negative-reading handling
// ----------------------------------------------------------------------------
bool shouldClampNegative(float activePower, ChannelRole role) {
    return activePower < 0.0f &&
           (role == CHANNEL_ROLE_LOAD || role == CHANNEL_ROLE_PV);
}

// ----------------------------------------------------------------------------
// WDRR weighting
// ----------------------------------------------------------------------------
bool roleHasSchedulingPriority(ChannelRole role) {
    return role == CHANNEL_ROLE_GRID || role == CHANNEL_ROLE_BATTERY;
}

float computeChannelWeight(const ChannelWeightInput& in, float powerShare,
                           float variabilityShare, const WeightConfig& cfg) {
    if (!in.active) return 0.0f;

    float weight = cfg.powerShare * powerShare +
                   cfg.variability * variabilityShare +
                   cfg.minBase;

    // Grid / battery are sampled more often (mains + storage flow matter most,
    // especially on 3-phase). Applied to the base share before the armed boost so
    // the boost stays a fixed, role-independent priority.
    if (roleHasSchedulingPriority(in.role)) weight *= cfg.rolePriorityMult;

    // Polarity-detection boost, gated on conduction (in.conducting, the caller's
    // pre-clamp "drawing power right now" signal). An armed channel only earns the
    // boost while actually conducting - so a CT clipped on an idle circuit waits at
    // normal cadence (no starvation) yet is sampled rapidly the moment load appears,
    // resolving in ~1 s. Using the pre-clamp signal (not activePower, which is 0 for
    // a clamped reversed load/PV) means reversed loads get the boost too.
    if (in.armed && in.conducting) weight += cfg.armedBoost;

    return weight;
}

void computeWeights(const ChannelWeightInput* in, uint8_t count, uint8_t startIndex,
                    const WeightConfig& cfg, float* outWeights) {
    // Indices outside the scheduled range are not part of the rotation.
    for (uint8_t i = 0; i < startIndex && i < count; i++) outWeights[i] = 0.0f;

    // First pass: totals across active channels. NaN guard keeps a single bad
    // reading from poisoning every share (and thus every weight).
    float totalAbsPower = 0.0f;
    float totalVariability = 0.0f;
    for (uint8_t i = startIndex; i < count; i++) {
        if (!in[i].active) continue;
        if (!std::isnan(in[i].activePower)) totalAbsPower += std::fabs(in[i].activePower);
        if (!std::isnan(in[i].variability)) totalVariability += in[i].variability;
    }

    // Second pass: per-channel weights.
    for (uint8_t i = startIndex; i < count; i++) {
        if (!in[i].active) {
            outWeights[i] = 0.0f;
            continue;
        }

        float powerShare = 0.0f;
        if (totalAbsPower > 0.0f && !std::isnan(in[i].activePower)) {
            powerShare = std::fabs(in[i].activePower) / totalAbsPower;
        }

        float variabilityShare = 0.0f;
        if (totalVariability > 0.0f && !std::isnan(in[i].variability)) {
            variabilityShare = in[i].variability / totalVariability;
        }

        outWeights[i] = computeChannelWeight(in[i], powerShare, variabilityShare, cfg);
    }
}

// ----------------------------------------------------------------------------
// WDRR scheduler core
// ----------------------------------------------------------------------------
uint8_t findStarvedChannel(const uint64_t* lastMillis, const bool* active,
                           uint8_t count, uint8_t startIndex, uint64_t now,
                           uint64_t maxGap) {
    for (uint8_t i = startIndex; i < count; i++) {
        if (!active[i]) continue;
        if (lastMillis[i] == 0) continue; // never baselined - priority slot handles it
        if (now - lastMillis[i] > maxGap) return i;
    }
    return NO_CHANNEL;
}

void wdrrAccumulate(const float* weights, float* deficits, const bool* active,
                    uint8_t count, uint8_t startIndex, float deficitBound) {
    for (uint8_t i = startIndex; i < count; i++) {
        if (active[i]) deficits[i] += weights[i];
        else deficits[i] = 0.0f;

        if (std::isnan(deficits[i])) deficits[i] = 0.0f;
        if (deficits[i] > deficitBound) deficits[i] = deficitBound;
        else if (deficits[i] < -deficitBound) deficits[i] = -deficitBound;
    }
}

uint8_t wdrrPick(float* deficits, const bool* active, uint8_t count,
                 uint8_t startIndex, uint8_t* cursor) {
    if (count <= startIndex) return NO_CHANNEL;
    const uint8_t range = count - startIndex;

    uint8_t bestChannel = NO_CHANNEL;
    float bestDeficit = 0.0f; // only meaningful once bestChannel is set

    // Iterate all schedulable channels starting one past the cursor, so that when
    // deficits are tied (e.g. a fleet of idle channels) service rotates instead of
    // the lowest index winning every time. The first active channel seeds the max
    // (no sentinel, so the result is independent of the deficit scale); strict '>'
    // thereafter makes the first-seen max win, preserving the rotation on ties.
    for (uint8_t offset = 0; offset < range; offset++) {
        uint8_t i = startIndex + ((*cursor - startIndex + 1 + offset) % range);
        if (!active[i]) continue;
        if (bestChannel == NO_CHANNEL || deficits[i] > bestDeficit) {
            bestDeficit = deficits[i];
            bestChannel = i;
        }
    }

    if (bestChannel != NO_CHANNEL) {
        deficits[bestChannel] -= 1.0f;
        *cursor = bestChannel;
    }

    return bestChannel;
}

} // namespace MeterLogic
