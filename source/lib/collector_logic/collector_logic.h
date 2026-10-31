// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

#include <ArduinoJson.h>

#include "gateway_policy.h"
#include "secrets_logic.h"

// Pure logic of the LAN collector (openspec capability: lan-collector): job set
// validation into a compact job table, the static template check, capture
// extraction, scheduling, backoff, window and cursor arithmetic, the part
// header encoder, chunking, the out-queue ring and the health state machine.
// Sockets, tasks, NVS, LittleFS, MQTT and SHA-256 live in the firmware; the
// firmware passes what it needs through callbacks. Host-tested (ArduinoJson is
// a native lib_dep).

namespace CollectorLogic {

// ---------------------------------------------------------------------------
// Limits (spec "Job schema and quotas")
// ---------------------------------------------------------------------------

inline constexpr size_t DOC_MAX = 5120;
inline constexpr size_t JOBS_MAX = 8;
inline constexpr size_t STEPS_MAX = 8;
inline constexpr size_t LOGIN_MAX = 4;
inline constexpr size_t CAPTURES_MAX = 8;
inline constexpr size_t VARS_MAX = 8;
inline constexpr size_t ID_MAX = 32;           // job, step, capture and var names
inline constexpr size_t SOURCE_ID_MAX = 12;    // "src_" + 4-8 hex
inline constexpr size_t CAPTURE_VALUE_MAX = 2048;
inline constexpr size_t CAPTURES_TOTAL_MAX = 4096;
inline constexpr size_t VAR_VALUE_MAX = 2048;
inline constexpr size_t DELIMITER_MAX = 32;    // capture after/before, expiry marker
inline constexpr size_t RUN_BYTES_MAX = 131072; // sum of data steps' max_bytes
inline constexpr size_t STEP_HEADERS_MAX = 16;
inline constexpr size_t CHAINS_PER_CAPTURE = 4; // distinct encoding chains a job applies to one capture
inline constexpr uint32_t BUDGET_DEFAULT = 2097152;
inline constexpr uint32_t BUDGET_MIN = 65536;
inline constexpr uint32_t BUDGET_MAX = 8388608;
inline constexpr size_t CHUNK_BYTES = 49152;
inline constexpr size_t COALESCE_BYTES = 5120;
inline constexpr size_t PART_HEADER_MAX = 768;
inline constexpr uint32_t WINDOW_MIN_S = 60;
inline constexpr uint32_t HEALTH_REPORT_INTERVAL_S = 300;
inline constexpr uint32_t ERR_AFTER_FAILURES = 3;
inline constexpr size_t REPORTED_MAX = 2048;
inline constexpr size_t CODE_MAX = 24; // [A-Z_]{1,24}

// ---------------------------------------------------------------------------
// Job table (validated, compact, all strings in `pool`)
// ---------------------------------------------------------------------------

enum class Encoding : uint8_t { NONE = 0, URL = 1, JSON = 2, B64 = 3 };

struct EncodingChain {
    uint8_t count;           // 0 = raw
    Encoding encodings[3];
};

struct Capture {
    char name[ID_MAX + 1];
    bool login;               // captured by a login step
    uint8_t step;             // index into login[] or steps[]
    bool fromHeader;          // false: body
    const char* key;          // body JSON top-level key, or nullptr
    const char* header;       // header name (from header), or nullptr
    const char* after;        // nullptr when absent
    const char* before;       // nullptr when absent
    uint8_t chainCount;       // encoding chains any step of the job applies to ${cap:name}
    EncodingChain chains[CHAINS_PER_CAPTURE];
};

struct Header {
    const char* name;
    const char* value; // templated
};

// The module request fields of a step (gateway-modules), strings in the pool.
struct StepArgs {
    const char* method;        // http/https, "GET" by default
    const char* path;          // templated, "/" by default
    uint8_t headerCount;
    Header headers[STEP_HEADERS_MAX];
    const char* body;          // templated text or nullptr
    const uint8_t* bodyBytes;  // decoded body_b64 or nullptr
    uint16_t bodyBytesLength;
    const char* certSha256;    // https
    GatewayLogic::ModbusAddress address; // modbus_tcp
    uint16_t count;            // modbus_tcp read count
    const char* payload;       // tcp text, templated
    const uint8_t* payloadBytes; // tcp/udp payload_b64
    uint16_t payloadBytesLength;
    uint32_t idleMs;           // tcp, 0 = default
    uint32_t waitMs;           // udp
};

struct Step {
    char id[ID_MAX + 1];
    StepArgs args;
    uint32_t timeoutMs; // effective (step value or job step_timeout_ms)
    uint32_t maxBytes;  // effective (step value or job max_bytes)
};

struct Var {
    char name[ID_MAX + 1];
    const char* text; // the value as expanded: strings as is, integers as decimal
};

struct Job {
    char id[ID_MAX + 1];
    uint32_t rev;
    bool enabled;
    char sourceId[SOURCE_ID_MAX + 1];
    char ip[16];
    uint8_t octets[4];
    uint16_t port;
    bool hasMac;              // target.mac, sent on every step of the job
    uint8_t mac[6];
    GatewayLogic::Module module;
    uint32_t intervalS;
    uint32_t jitterS;
    uint32_t stepTimeoutMs;
    uint32_t deadlineS;
    uint32_t maxBytes;
    bool batch;               // publish: batch
    // Reserved vars, with defaults applied.
    uint32_t overlapS;
    uint32_t backfillS;
    uint32_t windowMaxS;
    uint8_t varCount;
    Var vars[VARS_MAX];       // every var, reserved ones included, for ${var:x}
    // Session
    bool hasSession;
    uint32_t ttlS;
    uint8_t expiryStatusCount;
    uint16_t expiryStatus[4];
    const char* expiryMarker; // nullptr when absent
    uint8_t loginCount;
    Step login[LOGIN_MAX];
    uint8_t stepCount;
    Step steps[STEPS_MAX];
    uint8_t captureCount;
    Capture captures[CAPTURES_MAX];
    bool usesTime;            // any ${now} or ${cursor}
    bool usesCursor;          // any ${cursor}
};

struct JobTable {
    uint32_t rev;                 // job set rev (shadow), not a job rev
    uint8_t sha256[32];
    uint32_t budgetBytesH;
    uint8_t jobCount;
    Job jobs[JOBS_MAX];
    char* pool;                   // owned by the caller (PSRAM in firmware)
    size_t poolUsed;
    size_t poolCap;
};

inline constexpr size_t POOL_BYTES = 12288; // strings and decoded bytes of a <= 5120-byte doc, with margin

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

struct ApplyError {
    const char* code;            // nullptr when the set is valid
    char job[ID_MAX + 1];        // "" when not applicable
    char field[48];              // "" when not applicable, e.g. "interval_s", "steps[1]", "session.login[0]"
};

// What validation needs from the firmware.
struct ValidationContext {
    // SHA-256 of `length` bytes into `out` (mbedTLS on the device, a test stub on the host).
    void (*sha256)(const uint8_t* data, size_t length, uint8_t out[32]);
    // lan-gateway target rules: 0 OK, 1 TARGET_NOT_PRIVATE, 2 TARGET_NOT_ALLOWED.
    int (*targetCheck)(const uint8_t octets[4]);
    bool httpsAvailable;
    // Allocator of the parse JsonDocument (SpiRamAllocator on the device); nullptr = default.
    ArduinoJson::Allocator* allocator;
};

// Parses and validates the job document (`doc`, not the shadow envelope) into
// `out` (pool already set by the caller), in the spec order: BAD_JSON, BAD_JOB /
// QUOTA_EXCEEDED, WRITE_NOT_ALLOWED, TARGET_NOT_PRIVATE / TARGET_NOT_ALLOWED,
// BAD_TEMPLATE, MODULE_UNAVAILABLE. TOO_LARGE, BAD_HASH, STALE_REV and
// PERSIST_FAILED belong to the shadow layer in the firmware. Iterative, no
// recursion over steps. Returns false with `error` set on the first failure.
bool validateJobSet(const char* doc, size_t length, const ValidationContext& context, JobTable* out, ApplyError* error);

// ---------------------------------------------------------------------------
// Captures
// ---------------------------------------------------------------------------

enum class CaptureStatus { OK, MISSING };

// Extracts one capture from a reply (`headers` is the raw status line and
// header block, `body` the body). Value 1-2048 bytes, else MISSING.
CaptureStatus extractCapture(const Capture& capture, const char* headers, size_t headersLength, const uint8_t* body, size_t bodyLength,
                             uint8_t* out, size_t cap, size_t* length);

// Applies an encoding chain to `value` (same encoders as the template engine).
size_t applyChain(const EncodingChain& chain, const uint8_t* value, size_t length, uint8_t* out, size_t cap, uint8_t* scratch, size_t scratchCap);

// True when a reply matches the session expiry rule.
bool isExpired(const Job& job, int protoStatus, const uint8_t* body, size_t bodyLength);

// Capture echo scrub (spec "Raw publish topic and part format"): the shared
// scrub of add-gateway-secrets with `${cap:<name>}` placeholders. `forms` holds
// the raw and encoded forms of every capture the step expanded or captured
// (ScrubForm::name = capture name); `scratch` at least `cap` bytes. Sets
// `truncated` when the output was cut at `cap`, `redacted` when anything changed.
size_t scrubCaptures(uint8_t* buffer, size_t length, size_t cap, uint8_t* scratch, const SecretsLogic::ScrubForm* forms, size_t formCount,
                     bool* truncated, bool* redacted);

// ---------------------------------------------------------------------------
// Scheduling, backoff, window
// ---------------------------------------------------------------------------

// First slot after boot or a job change: now + random delay in [0, min(interval, 60)] s.
uint64_t firstSlotS(uint64_t nowS, uint32_t intervalS, uint32_t randomDelayS);
// The next slot t0 + k*interval strictly after `nowS` (missed slots skipped).
uint64_t nextSlotS(uint64_t t0S, uint32_t intervalS, uint64_t nowS);
// min(interval * 2^n, max(interval, 3600)) after n consecutive failures (n >= 1).
uint64_t backoffDelayS(uint32_t intervalS, uint32_t failures);

struct Window {
    uint64_t from; // device UTC seconds
    uint64_t to;
};

// start = max(cursor - overlap, now - backfill), end = min(start + w, now).
// The cursor resets to now - backfill when it is unset or later than now.
Window computeWindow(uint64_t nowS, uint64_t cursorS, bool cursorSet, uint32_t overlapS, uint32_t backfillS, uint32_t windowS);
uint32_t halveWindow(uint32_t windowS);                       // down to 60
uint32_t doubleWindow(uint32_t windowS, uint32_t windowMaxS); // up to window_max_s

// ---------------------------------------------------------------------------
// Step outcomes
// ---------------------------------------------------------------------------

enum class StepFate { PUBLISH_CONTINUE, PART_CONTINUE, STOP };

// What a step result means for the run (spec "Scheduling and step outcomes"):
// PUBLISH_CONTINUE for an OK reply, PART_CONTINUE for a header-only part that
// does not stop the run (modbus per-unit failures with connected true,
// MODBUS_EXCEPTION, BAD_RESPONSE), STOP otherwise.
StepFate stepFate(GatewayLogic::Module module, bool ok, const char* reasonCode, bool connected);

// ---------------------------------------------------------------------------
// Part header
// ---------------------------------------------------------------------------

struct PartHeader {
    const char* job;
    uint32_t rev;
    uint32_t set;
    const char* src;
    const char* boot;
    uint32_t seq;
    const char* step;
    bool login;
    GatewayLogic::Module module;
    uint32_t i;
    uint32_t n;
    uint32_t off;
    uint32_t len;
    uint32_t total;
    const char* sha;   // 64 hex or nullptr
    bool tr;
    const char* st;    // OK, FAILED, REJECTED
    const char* rc;    // nullptr on OK
    const char* rdt;   // executor reason_detail (only emitted when a code token and st != OK)
    bool hasPs;        // false: no protocol status (connect failure, modbus OK, tcp/udp), `ps` omitted
    int ps;
    bool tsKnown;
    uint64_t tsMs;     // `"ts":null` when !tsKnown
    uint64_t upMs;
    bool hasDur;       // `dur` omitted when false
    uint32_t durMs;
    bool hasWin;
    int64_t winFrom;
    int64_t winTo;
    uint32_t dq;
    bool rd;
    const char* ct;    // http/https Content-Type (<= 64, JSON-escaped) or nullptr
    const char* date;  // raw Date value; emitted only on data parts, <= 40 printable ASCII
    const char* addr;  // modbus canonical address or nullptr
    uint32_t cnt;
};

// One-line JSON header without the newline. 0 when it would exceed `cap` or
// PART_HEADER_MAX, or a string field is not printable ASCII. Field order:
// v job rev set src boot seq step ph mod i n off st ts up dq ps addr cnt ct
// win date tr rc rdt rd len total sha dur. Omitted: ps/dur without their
// has-flag; addr/cnt off modbus_tcp; ct off http/https or when absent, empty,
// longer than 64 bytes or not printable ASCII; win without hasWin; date off
// http/https data parts or when not 1-40 printable ASCII bytes; tr, rd when
// false; rc on OK; rdt on OK or when not a code token; sha when len is 0, on
// login parts or when null.
size_t encodePartHeader(const PartHeader& header, char* out, size_t cap);
bool isCodeToken(const char* text); // [A-Z_]{1,24}

inline uint32_t chunkCount(size_t total) { return total == 0 ? 1 : (uint32_t)((total + CHUNK_BYTES - 1) / CHUNK_BYTES); }

// Modbus register bytes: big-endian 16-bit words; bit tables packed as in the PDU (LSB first).
size_t packRegisters(const uint16_t* values, size_t count, bool bits, uint8_t* out, size_t cap);

// ---------------------------------------------------------------------------
// Out-queue (256 KB PSRAM ring in firmware)
// ---------------------------------------------------------------------------

// Identity of the run a part belongs to, so the publisher can tell the
// scheduler when every part of a run was written (cursor advance) or dropped.
struct PartTag {
    uint8_t job;   // index in the active table
    uint32_t seq;
    uint32_t body; // a run-unique body number; every chunk of a body shares it
    uint8_t gen = 0;    // job table generation, so tags never collide across adoptions
    bool alone = false; // a chunk of a multi-chunk body: published as its own message
};

class PartQueue {
public:
    void init(uint8_t* storage, size_t capacity);
    bool ready() const { return _storage != nullptr; }

    // Appends one part (header text without newline, payload). Drops oldest
    // whole bodies (every queued chunk of a body, including the rest of a body
    // whose first chunks were already published) to make room, never a body
    // with a pinned part; when that is not enough the new part is dropped
    // instead, together with the queued chunks of its body, and later chunks of
    // that body are refused. `notify` gets every dropped part's tag (published
    // false), the refused new part included. Returns false when the new part
    // was dropped. Pinned bytes never move.
    bool push(const char* sourceId, const PartTag& tag, const char* header, size_t headerLength, const uint8_t* payload, size_t payloadLength,
              void (*notify)(const PartTag& tag, bool published, void* context), void* context);

    struct View {
        const char* sourceId;
        size_t count;       // parts in the message
        size_t bytes;       // header + '\n' + payload of every part
        const uint8_t* parts[16];
        size_t partBytes[16];
        size_t headerLength[16];
    };
    // Pins the head part plus following parts of the same source while the
    // message stays <= 5120 bytes (a larger part, or a part tagged `alone`,
    // travels alone). False when empty.
    bool beginMessage(View* view);
    // `published` true: removes the pinned parts and notifies them published;
    // false: unpins them for the next pass.
    void endMessage(bool published, void (*notify)(const PartTag& tag, bool published, void* context), void* context);

    size_t bytes() const { return _used; }
    size_t parts() const { return _parts; }
    uint32_t droppedParts() const { return _droppedParts; }
    uint64_t droppedBytes() const { return _droppedBytes; }

private:
    // Records are stored back to back from offset 0 (`_used` bytes); removal
    // compacts the bytes behind the pinned prefix, so pinned bytes never move.
    void _removeBody(size_t firstRecord, const PartTag& tag, void (*notify)(const PartTag& tag, bool published, void* context), void* context);
    uint8_t* _storage = nullptr;
    size_t _capacity = 0;
    size_t _used = 0;
    size_t _parts = 0;
    size_t _pinned = 0; // parts pinned from the head
    size_t _pinnedBytes = 0;
    uint32_t _droppedParts = 0;
    uint64_t _droppedBytes = 0;
    bool _hasDroppedBody = false;
    PartTag _droppedBody{};
};

// Hourly token bucket.
struct TokenBucket {
    uint32_t limitH;
    double tokens;
    uint64_t lastMs;
};
void bucketInit(TokenBucket* bucket, uint32_t limitH, uint64_t nowMs); // starts full
void bucketRefill(TokenBucket* bucket, uint64_t nowMs);
bool bucketHasTokens(const TokenBucket& bucket);
void bucketSpend(TokenBucket* bucket, size_t bytes);

// ---------------------------------------------------------------------------
// Health
// ---------------------------------------------------------------------------

enum class HealthState : uint8_t { OK, ERR, WAIT, OFF };
const char* healthStateName(HealthState state); // ok, err, wait, off

struct Health {
    HealthState st;
    uint64_t since;              // last change of st or code (Unix s)
    char code[CODE_MAX + 1];     // "" = null
    char detail[CODE_MAX + 1];   // "" = null
    uint32_t consecutiveFailures;
};

// Returns true when st, code or detail changed (a report is due within 300 s).
bool healthOnRun(Health* health, bool failed, const char* code, const char* detail, uint64_t nowS);
bool healthOnWait(Health* health, const char* code, uint64_t nowS); // WAIT_CLOCK, PAUSED_*, LOW_HEAP, OFFLINE, BUDGET, NO_MEMORY
bool healthOnOff(Health* health, const char* code, uint64_t nowS); // code nullptr or GATEWAY_DISABLED
// Health-only changes are reported at most once per 300 s; apply results and switches at once.
bool reportDue(bool healthChanged, bool urgent, uint64_t lastReportS, uint64_t nowS);

} // namespace CollectorLogic
