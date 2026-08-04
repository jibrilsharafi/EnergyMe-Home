// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure helpers for the AWS IoT Device Shadow config feature (issue #159).
//
// Everything here is free-standing: no Arduino, no ArduinoJson, no FreeRTOS, no
// logging, no global state. The shadow module (src/shadow.cpp) owns the JSON
// document shaping and the MQTT plumbing; it feeds these functions the small,
// error-prone decisions (log-level name mapping, channel-key bounds, token
// formatting, version gating) so they can be unit-tested on the host (see
// test/test_shadow_logic) without any hardware or JSON library.
namespace ShadowLogic {

// Log levels mirror AdvancedLogger::LogLevel and Mqtt::_mqttLogLevelInt:
//   0=VERBOSE 1=DEBUG 2=INFO 3=WARNING 4=ERROR 5=FATAL
constexpr int LOG_LEVEL_INVALID = -1;

// Parse a canonical uppercase level name ("INFO", "DEBUG", ...) to its int
// (0..5). Matches the strict uppercase form used by the existing firmware
// command parser and by the cloud contract. Returns LOG_LEVEL_INVALID for an
// unknown name or a null pointer.
int logLevelFromString(const char* name);

// Int (0..5) to its canonical uppercase name. Returns nullptr if out of range.
const char* logLevelToString(int level);

// Transient levels (VERBOSE/DEBUG) are applied at runtime, never persisted, and
// auto-reverted after a timeout; persistent levels (INFO/WARNING/ERROR/FATAL)
// become the saved baseline. An out-of-range level is not transient.
bool isTransientLogLevel(int level);

// Parse an object key that must be a channel index ("0".."<channelCount-1>").
// Accepts only all-digit strings whose value is < channelCount. Returns true
// and sets *out on success; returns false (leaving *out untouched) for null,
// empty, non-numeric, or out-of-range keys.
bool parseChannelIndex(const char* key, uint8_t channelCount, uint8_t* out);

// Format a 16-char lowercase-hex client token ("%08x%08x") from two 32-bit
// randoms into out. Returns the number of characters written (16), or 0 if the
// buffer is too small to hold the token plus its null terminator (needs >= 17).
size_t formatClientToken(char* out, size_t outSize, uint32_t r1, uint32_t r2);

// The optimistic-lock document version is echoed on the delta-ack publish only
// when it is known (non-zero); a zero version means "not yet seen" and must be
// omitted to avoid a spurious 409 on the reconnect reported-first publish.
inline bool shouldSendVersion(uint32_t version) { return version != 0; }

// ----------------------------------------------------------------------------
// AWS IoT Commands helpers
// ----------------------------------------------------------------------------

// Extract the executionId from a Commands request topic:
//   $aws/commands/things/<thing>/executions/<execId>/request/<format>
// Writes <execId> (null-terminated) into out. Returns false for a null/malformed
// topic or if the id would not fit (a truncated id would publish the response to
// the wrong topic and the command would never be acked).
bool extractExecutionId(const char* topic, char* out, size_t outSize);

// A command is stale when it was created more than maxAgeSeconds ago (guards
// against acting on one queued during an offline window). createdAtUnix == 0
// means "no server timestamp available" -> not treated as stale (the caller
// logs and proceeds; the timestamp must come from the command payload since
// MQTT 3.1.1 carries no user properties). A future/equal timestamp (clock skew)
// is also not stale.
bool isCommandStale(uint64_t createdAtUnix, uint64_t nowUnix, uint64_t maxAgeSeconds);

// Parses a comma-separated channel-index list (e.g. "5" or "0,2,5") for the
// energy_reset command's string-typed `channels` param - the only shape AWS
// IoT Commands can actually deliver. Read-only scan: spec is never mutated or
// copied into a fixed buffer, so there is no length limit to enforce. Leading
// and trailing spaces around each token are trimmed; empty tokens (from a
// stray "5," or ",,") are skipped silently. Each non-empty token is validated
// with parseChannelIndex; an out-of-range or non-numeric token is skipped and
// causes *invalidTokenSeen to be set to true (so the caller can log a single
// WARNING), while parsing continues best-effort for the remaining tokens.
// Valid indices are written to validIndices in order, up to maxOut entries
// (extra valid tokens beyond maxOut are silently dropped - callers should size
// maxOut to >= channelCount). Returns the number of valid indices written.
size_t parseChannelList(const char* spec, uint8_t channelCount,
                         uint8_t* validIndices, size_t maxOut,
                         bool* invalidTokenSeen);

// Sanity check for an externally-supplied (shadow delta / REST) value for the
// per-channel "startMeasuringUnixTimeMs" field: 0 (never set) is always
// accepted; anything else must fall within [minPlausibleMs, maxPlausibleMs],
// bounds the caller sets comfortably around any real device timestamp.
// Catches unit mistakes in either direction - seconds instead of ms (too
// small) or microseconds/garbage (too large) - without hard-coding the
// bounds here.
bool isPlausibleStartMeasuringUnixTimeMs(uint64_t valueMs, uint64_t minPlausibleMs, uint64_t maxPlausibleMs);

// Decides whether an incoming JSON field value should overwrite the current
// startMeasuringUnixTimeMs. Absent (present=false) always means "leave
// unchanged" - this is what makes a config update that never mentions the
// field safe, whether it's a partial (shadow delta) or full (bulk PUT)
// update. Present-but-implausible also leaves it unchanged (caller should
// warn using the candidate it already has).
bool shouldAdoptStartMeasuringUnixTimeMs(bool present, uint64_t candidateMs, uint64_t minPlausibleMs, uint64_t maxPlausibleMs);

} // namespace ShadowLogic
