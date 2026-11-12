// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "phase_check.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace PhaseCheck {

namespace {

bool isSignedRole(ChannelRole role) {
    // Grid is signed: a correct grid line exports (180 degrees) whenever PV produces
    return role == CHANNEL_ROLE_GRID || role == CHANNEL_ROLE_BATTERY || role == CHANNEL_ROLE_INVERTER || role == CHANNEL_ROLE_BIDIRECTIONAL;
}

void roleBand(ChannelRole role, double &lo, double &hi) {
    if (role == CHANNEL_ROLE_PV) { lo = -65.0; hi = 35.0; return; }
    lo = -50.0; hi = 75.0;
}

double likelyAngleDeg(ChannelRole role) { return role == CHANNEL_ROLE_PV ? -10.0 : 20.0; }

constexpr double RAD_TO_DEG_D = 57.295779513082320876;
constexpr double DEG_TO_RAD_D = 0.017453292519943295769;

double wrap(double deg) {
    double d = std::fmod(deg + 180.0, 360.0);
    if (d < 0) d += 360.0;
    return d - 180.0;
}

double ang(const Complex &z) { return std::atan2(z.imag(), z.real()) * RAD_TO_DEG_D; }

Complex rot(double deg) { return std::polar(1.0, deg * DEG_TO_RAD_D); }

double shift(Phase vp, uint8_t phase) {
    return static_cast<double>(PhaseUtils::calculatePhaseShiftDeg(vp, static_cast<Phase>(phase)));
}

// Accumulates one minute into a channel; returns true when it conducted.
bool addTo(ChannelEvidence &c, const Complex &v, bool read) {
    if (!read) return false;
    const double mag = std::abs(v);
    if (mag < GATE_FRAC * c.ratingA * NOMINAL_V || mag == 0.0) return false;
    c.e += v / 60.0;
    c.axial += (v * v / mag) / 60.0;
    c.minutes += 1.0;
    return true;
}

struct Eval { bool ok; double spread; double psis[3]; };

Eval evalHypothesis(const LineEvidence &g, Phase vp, ChannelRole role, const Hypothesis &h, const bool evPair[3]) {
    const bool signedRole = isSignedRole(role);
    Eval r{};
    for (int i = 0; i < 3; i++) {
        const ChannelEvidence &c = g.ch[i];
        const double base = signedRole ? ang(c.axial) / 2.0 : ang(c.e);
        r.psis[i] = wrap(base + shift(vp, h.phase[i]) + (signedRole ? 0.0 : 180.0 * h.reverse[i]));
    }
    r.spread = 0.0;
    for (int k = 0; k < 3; k++) {
        if (!evPair[k]) continue;
        const int a = PAIR_A[k], b = PAIR_B[k];
        const Complex &d = g.pair[k].d;
        double diff;
        if (signedRole) {
            const double dd = std::abs(d) > 0.0 ? ang(d * d / std::abs(d)) : 0.0;
            diff = wrap(dd + 2.0 * (shift(vp, h.phase[a]) - shift(vp, h.phase[b]))) / 2.0;
        } else {
            diff = wrap(ang(d) + shift(vp, h.phase[a]) - shift(vp, h.phase[b]) +
                        180.0 * (static_cast<int>(h.reverse[a]) - static_cast<int>(h.reverse[b])));
        }
        r.spread = std::max(r.spread, std::fabs(diff));
    }
    if (signedRole) {
        r.ok = r.spread <= PAIR_TOL_DEG;
    } else {
        double lo, hi;
        roleBand(role, lo, hi);
        r.ok = r.spread <= PAIR_TOL_DEG;
        for (double x : r.psis) r.ok = r.ok && lo <= x && x <= hi;
    }
    return r;
}

// Distance of a hypothesis's mean channel angle to the role's typical angle.
double rankDistance(const LineEvidence &g, Phase vp, ChannelRole role, const Hypothesis &h) {
    Complex sum{0.0, 0.0};
    for (int i = 0; i < 3; i++) sum += rot(wrap(ang(g.ch[i].e) + shift(vp, h.phase[i]) + 180.0 * h.reverse[i]));
    return std::fabs(wrap(ang(sum) - likelyAngleDeg(role)));
}

const uint8_t PERMS[6][3] = {{1, 2, 3}, {1, 3, 2}, {2, 1, 3}, {2, 3, 1}, {3, 1, 2}, {3, 2, 1}};

} // namespace

const char *stateName(State s) {
    switch (s) {
        case State::Verified: return "verified";
        case State::Fix: return "fix";
        case State::Choose: return "choose";
        default: return "collecting";
    }
}

const char *singleStateName(SingleState s) {
    switch (s) {
        case SingleState::Ok: return "ok";
        case SingleState::Suspect: return "suspect";
        default: return "collecting";
    }
}

bool hasRoleBand(ChannelRole role) {
    return role == CHANNEL_ROLE_LOAD || role == CHANNEL_ROLE_PV;
}


bool Hypothesis::operator==(const Hypothesis &o) const {
    for (int i = 0; i < 3; i++) if (phase[i] != o.phase[i] || reverse[i] != o.reverse[i]) return false;
    return true;
}

void LineEvidence::addMinute(const Complex avgVa[3], const bool read[3]) {
    bool conducting[3];
    for (int i = 0; i < 3; i++) conducting[i] = addTo(ch[i], avgVa[i], read[i]);
    for (int k = 0; k < 3; k++) {
        const int a = PAIR_A[k], b = PAIR_B[k];
        if (!conducting[a] || !conducting[b]) continue;
        const Complex &va = avgVa[a], &vb = avgVa[b];
        const double w = std::min(std::abs(va), std::abs(vb)) / 60.0;
        pair[k].d += w * (va / std::abs(va)) * std::conj(vb / std::abs(vb));
        pair[k].minutes += 1.0;
    }
}

void addMinuteSingle(ChannelEvidence &ch, const Complex &avgVa, bool read) { addTo(ch, avgVa, read); }

double channelAngleDeg(const ChannelEvidence &c, Phase vp, uint8_t phase, bool reverse) {
    return wrap(ang(c.e) + shift(vp, phase) + 180.0 * reverse);
}

Result evaluate(const LineEvidence &g, Phase vp, ChannelRole role, const Hypothesis &configured) {
    Result res{};
    for (const ChannelEvidence &c : g.ch) {
        if (std::abs(c.e) < MIN_CH_WH) return res;
    }
    bool evPair[3];
    uint8_t evCount = 0;
    for (int k = 0; k < 3; k++) {
        evPair[k] = g.pair[k].minutes >= MIN_PAIR_MIN;
        evCount += evPair[k];
    }
    if (evCount < MIN_PAIRS) return res;

    const Eval cfg = evalHypothesis(g, vp, role, configured, evPair);
    if (cfg.ok) {
        res.state = State::Verified;
        return res;
    }
    const bool signedRole = isSignedRole(role);
    Hypothesis fitting[MAX_FITTING];
    uint8_t n = 0;
    for (const auto &perm : PERMS) {
        for (int rv = 0; rv < 8; rv++) {
            Hypothesis h{};
            for (int i = 0; i < 3; i++) {
                h.phase[i] = perm[i];
                // itertools.product([False, True], repeat=3): the first channel is the most significant bit
                h.reverse[i] = signedRole ? configured.reverse[i] : ((rv >> (2 - i)) & 1) != 0;
            }
            if (evalHypothesis(g, vp, role, h, evPair).ok) fitting[n++] = h;
            if (signedRole) break;  // reverse flags are never inferred for signed roles
        }
    }
    if (n == 0) {
        return res;  // unbalanced feeder, crosstalk or a CT on the wrong cable: keep measuring
    }
    if (hasRoleBand(role)) {
        std::stable_sort(fitting, fitting + n, [&](const Hypothesis &a, const Hypothesis &b) {
            return rankDistance(g, vp, role, a) < rankDistance(g, vp, role, b);
        });
    }
    auto emit = [&](State state, const Hypothesis *first, const Hypothesis *list, uint8_t count) {
        res.state = state;
        if (first) res.proposals[res.proposalCount++] = *first;
        for (uint8_t i = 0; i < count && res.proposalCount < MAX_PROPOSALS; i++) res.proposals[res.proposalCount++] = list[i];
        return res;
    };
    // The phases agree and only the band fails: a uniform error and an unusual load look
    // the same, so the configuration stays the first option.
    bool positive = true;
    for (double x : cfg.psis) positive = positive && std::cos(x * DEG_TO_RAD_D) > 0.0;
    if (cfg.spread <= PAIR_TOL_DEG && !signedRole && positive) return emit(State::Choose, &configured, fitting, n);
    // A reverse-only change is proposed alone only when it is the single reverse-only
    // hypothesis and also the most likely one; any phase change is a choice.
    uint8_t reverseOnly = 0;
    const Hypothesis *only = nullptr;
    for (uint8_t i = 0; i < n; i++) {
        if (std::memcmp(fitting[i].phase, configured.phase, sizeof(configured.phase)) == 0) {
            reverseOnly++;
            only = &fitting[i];
        }
    }
    if (reverseOnly == 1 && only == &fitting[0]) return emit(State::Fix, nullptr, fitting, 1);
    return emit(State::Choose, nullptr, fitting, n);
}

SingleResult checkSingle(const ChannelEvidence &c, Phase vp, ChannelRole role, uint8_t phase, bool reverse) {
    SingleResult res{};
    if (!hasRoleBand(role)) {
        res.state = SingleState::Ok;
        return res;
    }
    if (std::abs(c.e) < MIN_CH_WH) return res;
    double lo, hi;
    roleBand(role, lo, hi);
    bool configuredFits = false;
    for (uint8_t p = 1; p <= 3; p++) {
        for (int r = 0; r < 2; r++) {
            const double a = channelAngleDeg(c, vp, p, r != 0);
            if (a < lo || a > hi) continue;
            if (p == phase && (r != 0) == reverse) configuredFits = true;
            res.options[res.optionCount++] = SingleOption{p, r != 0};
        }
    }
    if (configuredFits) {
        res.state = SingleState::Ok;
        res.optionCount = 0;
        return res;
    }
    const double target = likelyAngleDeg(role);
    std::stable_sort(res.options, res.options + res.optionCount, [&](const SingleOption &a, const SingleOption &b) {
        return std::fabs(wrap(channelAngleDeg(c, vp, a.phase, a.reverse) - target)) <
               std::fabs(wrap(channelAngleDeg(c, vp, b.phase, b.reverse) - target));
    });
    res.state = SingleState::Suspect;
    return res;
}

static bool sameVerdict(const Result &a, const Result &b) {
    if (a.state != b.state || a.proposalCount != b.proposalCount) return false;
    for (uint8_t i = 0; i < a.proposalCount; i++) if (!(a.proposals[i] == b.proposals[i])) return false;
    return true;
}

bool StableVerdict::push(const Result &r, bool afterConfigWrite) {
    _run = (_run > 0 && sameVerdict(_last, r)) ? static_cast<uint8_t>(_run + 1) : 1;
    _last = r;
    // Collecting is shown at once: it never proposes anything.
    if (afterConfigWrite || r.state == State::Collecting || _run >= STABLE_EVALUATIONS) {
        const bool changed = !sameVerdict(_shown, r);
        _shown = r;
        return changed;
    }
    return false;
}

size_t detectLines(const ChannelView *channels, size_t count, const LineId *previous, size_t previousCount,
                   LineId *out, size_t maxOut) {
    size_t n = 0;
    bool used[64] = {};
    auto eligible = [&](size_t i) {
        const ChannelView &c = channels[i];
        return c.active && c.phase != PHASE_SPLIT_240 && c.groupLabel && c.groupLabel[0];
    };
    // Known lines survive phase and reverse changes while label, role and membership hold.
    for (size_t p = 0; p < previousCount && n < maxOut; p++) {
        const LineId &l = previous[p];
        bool ok = true;
        for (uint8_t i : l.ch) ok = ok && i < count && !used[i] && eligible(i) &&
                                    PhaseUtils::isValidVoltagePhase(channels[i].phase);
        if (!ok) continue;
        const ChannelView &a = channels[l.ch[0]];
        for (uint8_t i : l.ch) ok = ok && channels[i].role == a.role && std::strcmp(channels[i].groupLabel, a.groupLabel) == 0;
        size_t members = 0;  // no fourth channel joined the group
        for (size_t i = 0; i < count; i++) members += eligible(i) && std::strcmp(channels[i].groupLabel, a.groupLabel) == 0;
        if (!ok || members != 3) continue;
        out[n++] = l;
        for (uint8_t i : l.ch) used[i] = true;
    }
    // New lines: exactly three eligible channels share a label, with one role and three phases.
    for (size_t i = 0; i < count && n < maxOut; i++) {
        if (used[i] || !eligible(i)) continue;
        size_t idx[3];
        size_t members = 0;
        for (size_t j = 0; j < count; j++) {
            if (!eligible(j) || std::strcmp(channels[j].groupLabel, channels[i].groupLabel) != 0) continue;
            if (members < 3) idx[members] = j;
            members++;
        }
        if (members != 3 || idx[0] != i) continue;
        bool ok = true;
        uint8_t phases = 0;
        for (size_t k : idx) {
            ok = ok && !used[k] && channels[k].role == channels[i].role && PhaseUtils::isValidVoltagePhase(channels[k].phase);
            phases |= static_cast<uint8_t>(1u << channels[k].phase);
        }
        if (!ok || phases != 0x0E) continue;
        LineId l{{static_cast<uint8_t>(idx[0]), static_cast<uint8_t>(idx[1]), static_cast<uint8_t>(idx[2])}};
        out[n++] = l;
        for (size_t k : idx) used[k] = true;
    }
    return n;
}

} // namespace PhaseCheck
