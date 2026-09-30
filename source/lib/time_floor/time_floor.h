// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure rules for the NTP time floor: the earliest time the clock may be set to by
// an NTP answer. UnixTime::isValid only catches garbage, so a wrong-but-plausible
// answer (a server handing back 2023) used to be accepted, and every MQTT payload
// was stamped with it until the next resync happened to get a good answer.
//
// A floor that is itself in the future rejects every genuine answer until real
// time catches up, so an answer raises it at most to the ceiling (anchor + uptime),
// which never passes real time. The price: the floor lags real time by the install
// delay plus every power-off since, as only uptime moves the ceiling. An OTA to a
// newer commit catches it up through the build floor.
//
// src/customtime.cpp owns the state (an atomic floor and anchor, NVS persistence)
// and calls these; the rules live only here so they are host-tested
// (test/test_time_floor).
//
// All values are unix seconds as uint32_t (enough until 2106, past
// UnixTime::MAX_SECONDS). 0 means "no floor".
namespace TimeFloor {

// A unix time as a floor value: 0 (no floor) when it is not a plausible unix
// time in seconds. A garbage persisted value (e.g. from a restored backup) must
// never become a floor, since one past every genuine answer blocks all syncs.
uint32_t toFloor(uint64_t unixSeconds);

// Floor at boot: the later of the build floor (the source commit time) and the
// persisted floor.
uint32_t effective(uint64_t buildFloor, uint64_t persistedFloor);

// True when an NTP answer may set the clock. The tolerance lets a server that
// runs slightly behind the one that set the floor (failover resyncs seconds
// apart) through; the bogus answers this guards against are months or years off.
bool accepts(uint32_t floor, uint64_t candidateSeconds, uint32_t toleranceSeconds);

// Floor raised to a value: never lowered by a sync, even for a candidate that got
// in within the tolerance, and never raised to an implausible value.
uint32_t raisedBy(uint32_t floor, uint64_t acceptedSeconds);

// The most an answer may raise the floor to: the anchor, a lower bound of real time at
// uptime 0, plus the uptime. The anchor starts at the boot floor, which is not after
// real time (the build floor is a past commit, a persisted floor was capped by an
// earlier ceiling), and the uptime advances with real time, so a source that is
// consistently wrong in the future can never persist a floor ahead of real time.
// Crystal drift (tens of ppm of the uptime) can put the ceiling slightly ahead; the
// tolerance absorbs weeks of it, and a floor a few minutes ahead only blocks answers
// until real time passes it. Anchor 0 (no build floor, nothing persisted): 0, no raise.
uint32_t ceiling(uint32_t anchorSeconds, uint32_t uptimeSeconds);

// What lwIP hands over for a reply with an all-zero transmit timestamp, which an
// unsynchronized server sends and RFC 4330 says to discard. With SNTP_CHECK_RESPONSE
// 0 (the IDF default) lwIP passes it on, decoding NTP second 0 as era 1:
// 2036-02-07T06:28:16Z, far enough ahead to pass any floor.
constexpr uint64_t ZERO_TRANSMIT_SECONDS = 2085978496ULL;

// True for that artifact: never a genuine answer, rejected regardless of the floor.
bool isZeroTransmitArtifact(uint64_t candidateSeconds);

struct AnswerOutcome {
    bool accept;    // the answer may set the clock
    uint32_t floor; // the floor after it
};

// The sntp_sync_time override: an NTP answer checked against the floor, then raising it
// to the answer, at most to the ceiling. A single bogus future answer still steps the
// clock (as before the floor existed), but the floor it leaves is not past real time, so
// the next genuine answer steps it back.
AnswerOutcome onAnswer(uint32_t floor, uint32_t anchorSeconds, uint32_t uptimeSeconds,
                       uint64_t candidateSeconds, uint32_t toleranceSeconds);

struct ManualOutcome {
    uint32_t floor;
    uint32_t anchor;
};

// A manual time set: the floor is lowered to the value, never raised. Lowering is the
// way out of a wrong floor; raising would let a fast browser clock block NTP until real
// time caught up. The value is a claim about real time, so the anchor is lowered to keep
// the ceiling at or below it (0 when the uptime exceeds the value: no raise until the
// next boot). A 0 floor or anchor stays 0, and an implausible value changes nothing.
ManualOutcome onManualSet(uint32_t floor, uint32_t anchor, uint32_t uptimeSeconds, uint64_t manualSeconds);

} // namespace TimeFloor
