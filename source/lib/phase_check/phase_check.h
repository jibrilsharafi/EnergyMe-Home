// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>

#include "channel_types.h"
#include "phase_utils.h"

// Setup phase check (openspec change add-phase-check): pure decision logic, free of any
// Arduino / FreeRTOS dependency so it is host-tested (test/test_phase_check). The
// normative model is utils/phase_solver_ref.py; every constant and rule here mirrors it,
// and the host tests replay fixtures generated from it (utils/gen_phase_check_fixtures.py).
//
// Angles are degrees in the IEC frame of phase_utils.h. A channel's angle under a
// hypothesis is psi = raw + shift(voltagePhase, phase) + 180 * reverse, where raw is the
// angle of the energy pair the chip integrates against V1, before reverse, rotation and
// the load/PV clamp. A load lagging by phi on its own line reads psi = phi when phase and
// reverse are right.
//
// The check advises and never decides: `Fix` is a single reverse-only proposal, any phase
// change is `Choose`, and nothing is applied by the device.

namespace PhaseCheck {

using Complex = std::complex<double>;

constexpr double PAIR_TOL_DEG = 40.0;    // max |mean pair difference| for a fitting hypothesis
constexpr double GATE_FRAC = 0.03;       // conduction gate: minute apparent power vs CT rating x nominal V
constexpr double NOMINAL_V = 230.0;
constexpr double MIN_CH_WH = 1.0;        // apparent energy per channel before it is evaluable
constexpr double MIN_PAIR_MIN = 2.0;     // co-conducting minutes per pair before it is evaluable
constexpr uint8_t MIN_PAIRS = 2;         // one pair cannot see the third channel
constexpr uint8_t STABLE_EVALUATIONS = 2; // same answer on consecutive minute evaluations before it is shown
constexpr uint8_t MAX_FITTING = 48;      // 6 permutations x 8 reverse sets
constexpr uint8_t MAX_PROPOSALS = 8;     // shown candidates; the fleet never had more than 3

enum class State : uint8_t { Collecting, Verified, Fix, Choose };
enum class SingleState : uint8_t { Collecting, Ok, Suspect };

const char *stateName(State state);
const char *singleStateName(SingleState state);

// Signed roles keep both power signs: angles are axial (mod 180) and reverse is never
// inferred. Load, grid and PV have a role band.
bool isSignedRole(ChannelRole role);
bool hasRoleBand(ChannelRole role);
void roleBand(ChannelRole role, double &lo, double &hi);
double likelyAngleDeg(ChannelRole role); // typical angle of a correct channel, for ranking

struct ChannelEvidence {
    double ratingA = 0.0;
    Complex e{0.0, 0.0};      // sum of minute phasor / 60 (VA h): energy-weighted angle
    Complex axial{0.0, 0.0};  // doubled-angle sum for signed roles
    double minutes = 0.0;     // conducting minutes
};

struct PairEvidence {
    Complex d{0.0, 0.0};
    double weight = 0.0;
    double minutes = 0.0;     // co-conducting minutes
};

// Pairs in itertools.combinations order: (0,1), (0,2), (1,2).
constexpr uint8_t PAIR_A[3] = {0, 0, 1};
constexpr uint8_t PAIR_B[3] = {1, 2, 2};

struct LineEvidence {
    ChannelEvidence ch[3];
    PairEvidence pair[3];
    // One closed minute. avgVa[i] is the time-averaged raw phasor (VA) of channel i over
    // the minute; read[i] false when the channel was not read in it. A channel conducts
    // when its minute apparent power passes the gate.
    void addMinute(const Complex avgVa[3], const bool read[3]);
};

// Time-weighted minute average of one channel's raw phasor. Each reading adds its power
// (VA, raw frame) times the elapsed time since the channel's previous reading, so channel 0
// (read every window) and a multiplexed channel read every few windows average alike.
// Readings with no elapsed time (the first one, a discarded or rebased one) add nothing.
struct MinuteBucket {
    Complex sumVaMs{0.0, 0.0};
    double ms = 0.0;
    void add(const Complex &va, uint64_t deltaMs) {
        if (deltaMs == 0) return;
        sumVaMs += va * static_cast<double>(deltaMs);
        ms += static_cast<double>(deltaMs);
    }
    bool read() const { return ms > 0.0; }
    Complex average() const { return ms > 0.0 ? sumVaMs / ms : Complex{0.0, 0.0}; }
    void clear() { *this = MinuteBucket(); }
};

// Single channel outside any line: same accumulation, no pairs.
void addMinuteSingle(ChannelEvidence &ch, const Complex &avgVa, bool read);

struct Hypothesis {
    uint8_t phase[3];   // 1..3, the phase each line channel is assigned to
    bool reverse[3];
    bool operator==(const Hypothesis &o) const;
};

struct Result {
    State state = State::Collecting;
    uint8_t proposalCount = 0;
    Hypothesis proposals[MAX_PROPOSALS];  // most likely first
    double angles[3] = {0.0, 0.0, 0.0};   // channel angles under the configuration (valid unless collecting on minimums)
    bool noFit = false;                   // collecting because no hypothesis fits (not because of minimums)
};

Result evaluate(const LineEvidence &g, Phase voltagePhase, ChannelRole role, const Hypothesis &configured);

// Channel angle under a phase and reverse flag (non-signed frame), for the readings the
// page shows under a candidate.
double channelAngleDeg(const ChannelEvidence &c, Phase voltagePhase, uint8_t phase, bool reverse);

struct SingleOption { uint8_t phase; bool reverse; };
struct SingleResult {
    SingleState state = SingleState::Collecting;
    uint8_t optionCount = 0;
    SingleOption options[6];  // most likely first
};

// Advice for a load, grid or PV channel outside a line on a three-phase install. A wrong
// phase plus a reversed CT is 60 degrees off and can pass; only a line can catch it.
SingleResult checkSingle(const ChannelEvidence &c, Phase voltagePhase, ChannelRole role, uint8_t phase, bool reverse);

// Shows a state only once STABLE_EVALUATIONS consecutive evaluations agree; an
// evaluation right after a configuration write is shown at once.
class StableVerdict {
public:
    // Returns true when `r` is now the shown result.
    bool push(const Result &r, bool afterConfigWrite);
    const Result &shown() const { return _shown; }
    void reset() { *this = StableVerdict(); }
private:
    Result _shown{};
    Result _last{};
    uint8_t _run = 0;
};

bool sameVerdict(const Result &a, const Result &b);

// ---- line detection ----------------------------------------------------------------

struct ChannelView {
    bool active;
    ChannelRole role;
    uint32_t phase;         // Phase value, 4 = split-phase 240 V
    const char *groupLabel;
};

struct LineId { uint8_t ch[3]; };  // ascending channel indices

// Lines keep their identity (group label + channel set) while phases and reverse flags
// change; a new line needs three channels with the same label and role on three
// different phases. `previous` are the lines known before this configuration write.
// Returns the number of lines written to `out` (at most maxOut).
size_t detectLines(const ChannelView *channels, size_t channelCount,
                   const LineId *previous, size_t previousCount,
                   LineId *out, size_t maxOut);

} // namespace PhaseCheck
