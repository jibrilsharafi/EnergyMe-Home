// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure rules for the NTP time floor: the earliest time the clock may be set to by
// an NTP answer. UnixTime::isValid only catches garbage, so a wrong-but-plausible
// answer (a server handing back 2023) used to be accepted, and every MQTT payload
// was stamped with it until the next resync happened to get a good answer.
//
// src/customtime.cpp owns the state (an atomic floor, NVS persistence) and calls
// these; the rules live only here so they are host-tested (test/test_time_floor).
//
// All values are unix seconds as uint32_t (enough until 2106, past
// UnixTime::MAX_SECONDS). 0 means "no floor".
namespace TimeFloor {

// A unix time as a floor value: 0 (no floor) when it is not a plausible unix
// time in seconds. A garbage persisted value (e.g. from a restored backup) must
// never become a floor, since one past every genuine answer blocks all syncs.
uint32_t toFloor(uint64_t unixSeconds);

// Floor at boot: the later of the build floor (the source commit time) and the
// floor persisted by the last accepted sync.
uint32_t effective(uint64_t buildFloor, uint64_t persistedFloor);

// True when an NTP answer may set the clock. The tolerance lets a server that
// runs slightly behind the one that set the floor (failover resyncs seconds
// apart) through; the bogus answers this guards against are months or years off.
bool accepts(uint32_t floor, uint64_t candidateSeconds, uint32_t toleranceSeconds);

// Floor after an accepted answer: never lowered by a sync, even for a candidate
// that got in within the tolerance.
uint32_t raisedBy(uint32_t floor, uint64_t acceptedSeconds);

// What lwIP hands over for a reply with an all-zero transmit timestamp, which an
// unsynchronized server sends and RFC 4330 says to discard. With SNTP_CHECK_RESPONSE
// 0 (the IDF default) lwIP passes it on, decoding NTP second 0 as era 1:
// 2036-02-07T06:28:16Z, far enough ahead to pass any floor.
constexpr uint64_t ZERO_TRANSMIT_SECONDS = 2085978496ULL;

// True for that artifact: never a genuine answer, rejected regardless of the floor.
bool isZeroTransmitArtifact(uint64_t candidateSeconds);

} // namespace TimeFloor
