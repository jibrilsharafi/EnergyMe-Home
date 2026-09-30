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
// time catches up, so only an answer corroborated by the previous accepted one
// may raise it (see corroborates).
//
// src/customtime.cpp owns the state (an atomic floor, the corroboration base, NVS
// persistence) and calls these; the rules live only here so they are host-tested
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

// True when an accepted answer agrees with the base, the previous accepted answer
// (wall time and uptime when it arrived): the wall time moved as far as the uptime
// did, within the tolerance. Only then may the answer raise the floor, so a single
// bogus future answer still steps the clock (as before the floor existed) but the
// next genuine one undoes it instead of being rejected. A base wall of 0 is "no
// base" (none since boot); uptime going backwards or an implausible wall never
// corroborates.
bool corroborates(uint32_t baseWallSeconds, uint32_t baseUptimeSeconds,
                  uint64_t wallSeconds, uint32_t uptimeSeconds, uint32_t toleranceSeconds);

// Floor after a corroborated answer: never lowered by a sync, even for a
// candidate that got in within the tolerance.
uint32_t raisedBy(uint32_t floor, uint64_t acceptedSeconds);

// What lwIP hands over for a reply with an all-zero transmit timestamp, which an
// unsynchronized server sends and RFC 4330 says to discard. With SNTP_CHECK_RESPONSE
// 0 (the IDF default) lwIP passes it on, decoding NTP second 0 as era 1:
// 2036-02-07T06:28:16Z, far enough ahead to pass any floor.
constexpr uint64_t ZERO_TRANSMIT_SECONDS = 2085978496ULL;

// True for that artifact: never a genuine answer, rejected regardless of the floor.
bool isZeroTransmitArtifact(uint64_t candidateSeconds);

} // namespace TimeFloor
