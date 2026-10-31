// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

// Job set validation (spec "Job schema and quotas", "Jobs are read-only",
// "Placeholders and encodings", "Captures") and the binding hash. Stages run
// over the whole set in the spec order, so the first failure reported is the
// first failure of the earliest stage. Iterative: no recursion over steps.

#include <cstdio>
#include <cstring>

#include "collector_logic.h"
#include "gateway_logic.h"
#include "gateway_template.h"

namespace CollectorLogic {

using GatewayLogic::Module;

namespace {

constexpr const char *BAD_JSON = "BAD_JSON";
constexpr const char *BAD_JOB = "BAD_JOB";
constexpr const char *QUOTA_EXCEEDED = "QUOTA_EXCEEDED";
constexpr const char *WRITE_NOT_ALLOWED = "WRITE_NOT_ALLOWED";
constexpr const char *TARGET_NOT_PRIVATE = "TARGET_NOT_PRIVATE";
constexpr const char *TARGET_NOT_ALLOWED = "TARGET_NOT_ALLOWED";
constexpr const char *BAD_TEMPLATE = "BAD_TEMPLATE";
constexpr const char *MODULE_UNAVAILABLE = "MODULE_UNAVAILABLE";

constexpr int64_t CLOCK_OFFSET_MAX_S = 604800;
constexpr int64_t INTERVAL_MIN_S = 60;
constexpr int64_t INTERVAL_MAX_S = 86400;
constexpr int64_t JITTER_MAX_S = 300;
constexpr int64_t DEADLINE_MIN_S = 5;
constexpr int64_t DEADLINE_MAX_S = 300;
constexpr int64_t DEADLINE_DEFAULT_S = 60;
constexpr int64_t STEP_TIMEOUT_MIN_MS = 100;
constexpr int64_t STEP_TIMEOUT_MAX_MS = 10000;
constexpr int64_t STEP_TIMEOUT_DEFAULT_MS = 5000;
constexpr int64_t MAX_BYTES_DEFAULT = 16384;
constexpr int64_t OVERLAP_MAX_S = 3600;
constexpr int64_t BACKFILL_DEFAULT_S = 3600;
constexpr int64_t BACKFILL_MAX_S = 86400;
constexpr int64_t WINDOW_MAX_DEFAULT_S = 3600;
constexpr int64_t TTL_MIN_S = 60;
constexpr int64_t TTL_MAX_S = 86400;
constexpr size_t EXPIRY_STATUS_MAX = 4;
constexpr size_t CAPTURE_KEY_MAX = 64;
constexpr size_t TCP_PAYLOAD_MAX = 5760;
constexpr size_t UDP_PAYLOAD_MAX = 1460;
constexpr int64_t IDLE_MS_MIN = 50;
constexpr int64_t IDLE_MS_MAX = 5000;
constexpr int64_t WAIT_MS_MAX = 5000;
constexpr size_t MODBUS_WRITE_VALUES_MAX = 123;
constexpr size_t STEP_SLOTS = LOGIN_MAX + STEPS_MAX; // login steps, then data steps

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

void setError(ApplyError *error, const char *code, const char *job, const char *field) {
    error->code = code;
    snprintf(error->job, sizeof(error->job), "%s", job != nullptr ? job : "");
    snprintf(error->field, sizeof(error->field), "%s", field != nullptr ? field : "");
}

bool fail(ApplyError *error, const char *code, const char *job, const char *field) {
    setError(error, code, job, field);
    return false;
}

bool failStep(ApplyError *error, const char *code, const char *job, bool login, size_t index) {
    char field[sizeof(error->field)];
    snprintf(field, sizeof(field), login ? "session.login[%u]" : "steps[%u]", static_cast<unsigned>(index));
    return fail(error, code, job, field);
}

bool isIdChar(char c, bool upper) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-' || (upper && c >= 'A' && c <= 'Z');
}

// Job and step ids `[a-z0-9_.-]{1,32}`; var and capture names also allow upper case.
bool isId(const char *text, size_t length, bool upper) {
    if (length < 1 || length > ID_MAX) return false;
    for (size_t i = 0; i < length; i++) {
        if (!isIdChar(text[i], upper)) return false;
    }
    return true;
}

bool isSourceId(const char *text, size_t length) {
    if (length < 8 || length > SOURCE_ID_MAX || memcmp(text, "src_", 4) != 0) return false;
    for (size_t i = 4; i < length; i++) {
        char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

char *poolAlloc(JobTable *table, size_t length) {
    if (table->pool == nullptr || length > table->poolCap - table->poolUsed) return nullptr;
    char *p = table->pool + table->poolUsed;
    table->poolUsed += length;
    return p;
}

const char *poolString(JobTable *table, const char *text, size_t length) {
    char *p = poolAlloc(table, length + 1);
    if (p == nullptr) return nullptr;
    memcpy(p, text, length);
    p[length] = '\0';
    return p;
}

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Strict padded base64. Returns false on any malformation.
bool decodeBase64(const char *in, size_t length, uint8_t *out, size_t cap, size_t *outLength) {
    *outLength = 0;
    if (length % 4 != 0) return false;
    size_t used = 0;
    for (size_t i = 0; i < length; i += 4) {
        int v[4];
        size_t pad = 0;
        for (size_t k = 0; k < 4; k++) {
            char c = in[i + k];
            if (c == '=') {
                // Padding only in the last quad, only in its last two positions, and nothing after it.
                if (i + 4 != length || k < 2) return false;
                pad++;
                v[k] = 0;
                continue;
            }
            if (pad > 0) return false;
            v[k] = base64Value(c);
            if (v[k] < 0) return false;
        }
        uint32_t bits = (static_cast<uint32_t>(v[0]) << 18) | (static_cast<uint32_t>(v[1]) << 12) | (static_cast<uint32_t>(v[2]) << 6) |
                        static_cast<uint32_t>(v[3]);
        size_t bytes = 3 - pad;
        if (used + bytes > cap) return false;
        out[used++] = static_cast<uint8_t>(bits >> 16);
        if (bytes > 1) out[used++] = static_cast<uint8_t>(bits >> 8);
        if (bytes > 2) out[used++] = static_cast<uint8_t>(bits);
    }
    *outLength = used;
    return true;
}

// ---------------------------------------------------------------------------
// JSON access
// ---------------------------------------------------------------------------

enum class Got { ABSENT, OK, BAD };

Got getInt(JsonVariantConst value, int64_t min, int64_t max, int64_t *out) {
    if (value.isNull()) return Got::ABSENT;
    if (!value.is<int64_t>()) return Got::BAD;
    int64_t v = value.as<int64_t>();
    if (v < min || v > max) return Got::BAD;
    *out = v;
    return Got::OK;
}

// A string without NUL bytes, `min` to `max` bytes long.
Got getString(JsonVariantConst value, size_t min, size_t max, const char **text, size_t *length) {
    if (value.isNull()) return Got::ABSENT;
    if (!value.is<const char *>()) return Got::BAD;
    JsonString s = value.as<JsonString>();
    if (s.c_str() == nullptr || s.size() < min || s.size() > max || memchr(s.c_str(), '\0', s.size()) != nullptr) return Got::BAD;
    *text = s.c_str();
    *length = s.size();
    return Got::OK;
}

bool keyIn(const char *key, const char *const *allowed, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(key, allowed[i]) == 0) return true;
    }
    return false;
}

// First key of `object` not in `allowed`, or nullptr.
const char *unknownKey(JsonObjectConst object, const char *const *allowed, size_t count) {
    for (JsonPairConst pair : object) {
        if (!keyIn(pair.key().c_str(), allowed, count)) return pair.key().c_str();
    }
    return nullptr;
}

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

const char *const TOP_KEYS[] = {"v", "budget_bytes_h", "jobs"};
const char *const JOB_KEYS[] = {"id",          "rev",        "enabled",   "source_id", "target", "module",  "interval_s", "jitter_s",
                                "step_timeout_ms", "deadline_s", "max_bytes", "vars",      "session", "steps", "publish"};
const char *const TARGET_KEYS[] = {"ip", "port"};
const char *const SESSION_KEYS[] = {"login", "ttl_s", "expiry"};
const char *const EXPIRY_KEYS[] = {"status", "marker"};
const char *const STEP_KEYS[] = {"id", "args", "timeout_ms", "max_bytes", "captures"};
const char *const CAPTURE_KEYS[] = {"name", "from", "key", "header", "after", "before"};
const char *const HTTP_KEYS[] = {"method", "path", "headers", "body", "body_b64"};
const char *const HTTPS_KEYS[] = {"method", "path", "headers", "body", "body_b64", "cert_sha256"};
const char *const MODBUS_KEYS[] = {"address", "count", "values", "write_multiple", "verify"};
const char *const TCP_KEYS[] = {"payload", "payload_b64", "idle_ms"};
const char *const UDP_KEYS[] = {"payload_b64", "wait_ms"};

// ---------------------------------------------------------------------------
// Validation state
// ---------------------------------------------------------------------------

struct Validation {
    const ValidationContext &context;
    JobTable *table;
    ApplyError *error;
    bool modbusWrite[JOBS_MAX][STEP_SLOTS];
};

Step &stepAt(Job &job, bool login, size_t index) { return login ? job.login[index] : job.steps[index]; }

bool storeString(Validation &v, const Job &job, const char *text, size_t length, const char **out) {
    *out = poolString(v.table, text, length);
    return *out != nullptr || fail(v.error, QUOTA_EXCEEDED, job.id, "doc");
}

// Stores a base64 field decoded in the pool.
bool storeBase64(Validation &v, const Job &job, const char *field, const char *text, size_t length, size_t maxBytes, const uint8_t **out,
                 uint16_t *outLength) {
    size_t cap = length / 4 * 3;
    size_t start = v.table->poolUsed;
    uint8_t *buffer = reinterpret_cast<uint8_t *>(poolAlloc(v.table, cap));
    if (buffer == nullptr && cap > 0) return fail(v.error, QUOTA_EXCEEDED, job.id, "doc");
    size_t decoded = 0;
    if (!decodeBase64(text, length, buffer, cap, &decoded) || decoded > maxBytes) return fail(v.error, BAD_JOB, job.id, field);
    v.table->poolUsed = start + decoded;
    *out = decoded > 0 ? buffer : nullptr;
    *outLength = static_cast<uint16_t>(decoded);
    return true;
}

// ---------------------------------------------------------------------------
// Stage 1: schema and quotas
// ---------------------------------------------------------------------------

bool parseHttpArgs(Validation &v, Job &job, JsonObjectConst args, StepArgs &out) {
    const char *text = nullptr;
    size_t length = 0;
    Got got = getString(args["method"], 1, 16, &text, &length);
    if (got == Got::BAD) return fail(v.error, BAD_JOB, job.id, "method");
    if (got == Got::ABSENT) {
        text = "GET";
        length = 3;
    }
    if (!storeString(v, job, text, length, &out.method)) return false;
    if (!GatewayLogic::isAllowedHttpMethod(out.method)) return fail(v.error, BAD_JOB, job.id, "method");

    got = getString(args["path"], 1, GatewayLogic::HTTP_PATH_MAX_LEN, &text, &length);
    if (got == Got::BAD) return fail(v.error, BAD_JOB, job.id, "path");
    if (got == Got::ABSENT) {
        text = "/";
        length = 1;
    }
    if (!storeString(v, job, text, length, &out.path)) return false;
    if (!GatewayLogic::isValidHttpPath(out.path)) return fail(v.error, BAD_JOB, job.id, "path");

    JsonVariantConst headers = args["headers"];
    if (!headers.isNull()) {
        if (!headers.is<JsonObjectConst>()) return fail(v.error, BAD_JOB, job.id, "headers");
        JsonObjectConst object = headers.as<JsonObjectConst>();
        if (object.size() > STEP_HEADERS_MAX) return fail(v.error, QUOTA_EXCEEDED, job.id, "headers");
        for (JsonPairConst pair : object) {
            const char *name = pair.key().c_str();
            const char *value = nullptr;
            size_t valueLength = 0;
            if (!GatewayLogic::isValidHeaderName(name) || GatewayLogic::isReservedHeaderName(name) ||
                getString(pair.value(), 0, DOC_MAX, &value, &valueLength) != Got::OK)
                return fail(v.error, BAD_JOB, job.id, "headers");
            Header &header = out.headers[out.headerCount];
            if (!storeString(v, job, name, strlen(name), &header.name) || !storeString(v, job, value, valueLength, &header.value)) return false;
            if (!GatewayLogic::isValidHeaderValue(header.value)) return fail(v.error, BAD_JOB, job.id, "headers");
            out.headerCount++;
        }
    }

    bool hasBody = !args["body"].isNull();
    if (hasBody && !args["body_b64"].isNull()) return fail(v.error, BAD_JOB, job.id, "body_b64");
    if (hasBody) {
        if (getString(args["body"], 0, DOC_MAX, &text, &length) != Got::OK) return fail(v.error, BAD_JOB, job.id, "body");
        if (!storeString(v, job, text, length, &out.body)) return false;
    }
    if (!args["body_b64"].isNull()) {
        if (getString(args["body_b64"], 0, DOC_MAX, &text, &length) != Got::OK) return fail(v.error, BAD_JOB, job.id, "body_b64");
        if (!storeBase64(v, job, "body_b64", text, length, TCP_PAYLOAD_MAX, &out.bodyBytes, &out.bodyBytesLength)) return false;
    }

    if (job.module == Module::HTTPS) {
        uint8_t digest[GatewayLogic::DIGEST_BYTES];
        if (getString(args["cert_sha256"], 64, 64, &text, &length) != Got::OK || !GatewayLogic::parseHexDigest(text, digest))
            return fail(v.error, BAD_JOB, job.id, "cert_sha256");
        if (!storeString(v, job, text, length, &out.certSha256)) return false;
    }
    return true;
}

bool parseModbusArgs(Validation &v, Job &job, JsonObjectConst args, StepArgs &out, bool *write) {
    const char *text = nullptr;
    size_t length = 0;
    if (getString(args["address"], 1, GatewayLogic::MODBUS_ADDRESS_BUFFER - 1, &text, &length) != Got::OK ||
        !GatewayLogic::parseModbusAddress(text, &out.address))
        return fail(v.error, BAD_JOB, job.id, "address");

    JsonVariantConst values = args["values"];
    if (!values.isNull()) {
        // A write: refused as WRITE_NOT_ALLOWED by the classing stage, so only the shape is checked here.
        if (!values.is<JsonArrayConst>()) return fail(v.error, BAD_JOB, job.id, "values");
        JsonArrayConst array = values.as<JsonArrayConst>();
        if (array.size() < 1 || array.size() > MODBUS_WRITE_VALUES_MAX) return fail(v.error, BAD_JOB, job.id, "values");
        for (JsonVariantConst item : array) {
            int64_t value = 0;
            if (getInt(item, 0, 65535, &value) != Got::OK) return fail(v.error, BAD_JOB, job.id, "values");
        }
        *write = true;
    } else if (!args["write_multiple"].isNull()) {
        return fail(v.error, BAD_JOB, job.id, "write_multiple");
    } else if (!args["verify"].isNull()) {
        return fail(v.error, BAD_JOB, job.id, "verify");
    }
    if (!args["write_multiple"].isNull() && !args["write_multiple"].is<bool>()) return fail(v.error, BAD_JOB, job.id, "write_multiple");
    if (!args["verify"].isNull() && !args["verify"].is<bool>()) return fail(v.error, BAD_JOB, job.id, "verify");

    int64_t count = 0;
    Got got = getInt(args["count"], 1, 2000, &count);
    if (got == Got::BAD || (got == Got::ABSENT && !*write)) return fail(v.error, BAD_JOB, job.id, "count");
    if (got == Got::OK) {
        uint8_t fc = GatewayLogic::functionFor(out.address.table, false, static_cast<size_t>(count), false);
        if (!GatewayLogic::isValidModbusReadCount(fc, static_cast<uint32_t>(count)) ||
            !GatewayLogic::modbusRangeFits(out.address.address, static_cast<uint32_t>(count)))
            return fail(v.error, BAD_JOB, job.id, "count");
        out.count = static_cast<uint16_t>(count);
    }
    return true;
}

bool parseRawArgs(Validation &v, Job &job, JsonObjectConst args, StepArgs &out) {
    const char *text = nullptr;
    size_t length = 0;
    bool udp = job.module == Module::UDP;
    if (!udp) {
        bool hasText = !args["payload"].isNull();
        if (hasText && !args["payload_b64"].isNull()) return fail(v.error, BAD_JOB, job.id, "payload_b64");
        if (hasText) {
            if (getString(args["payload"], 0, TCP_PAYLOAD_MAX, &text, &length) != Got::OK) return fail(v.error, BAD_JOB, job.id, "payload");
            if (!storeString(v, job, text, length, &out.payload)) return false;
        }
        int64_t idle = 0;
        if (getInt(args["idle_ms"], IDLE_MS_MIN, IDLE_MS_MAX, &idle) == Got::BAD) return fail(v.error, BAD_JOB, job.id, "idle_ms");
        out.idleMs = static_cast<uint32_t>(idle);
    } else {
        int64_t wait = 0;
        if (getInt(args["wait_ms"], 0, WAIT_MS_MAX, &wait) == Got::BAD) return fail(v.error, BAD_JOB, job.id, "wait_ms");
        out.waitMs = static_cast<uint32_t>(wait);
    }
    JsonVariantConst b64 = args["payload_b64"];
    if (udp && b64.isNull()) return fail(v.error, BAD_JOB, job.id, "payload_b64");
    if (!b64.isNull()) {
        if (getString(b64, 0, DOC_MAX, &text, &length) != Got::OK) return fail(v.error, BAD_JOB, job.id, "payload_b64");
        if (!storeBase64(v, job, "payload_b64", text, length, udp ? UDP_PAYLOAD_MAX : TCP_PAYLOAD_MAX, &out.payloadBytes,
                         &out.payloadBytesLength))
            return false;
        if (udp && out.payloadBytesLength == 0) return fail(v.error, BAD_JOB, job.id, "payload_b64");
    }
    return true;
}

bool parseArgs(Validation &v, Job &job, JsonVariantConst value, StepArgs &out, bool *write) {
    if (!value.is<JsonObjectConst>()) return fail(v.error, BAD_JOB, job.id, "args");
    JsonObjectConst args = value.as<JsonObjectConst>();
    const char *unknown = nullptr;
    switch (job.module) {
        case Module::HTTP: unknown = unknownKey(args, HTTP_KEYS, COUNT_OF(HTTP_KEYS)); break;
        case Module::HTTPS: unknown = unknownKey(args, HTTPS_KEYS, COUNT_OF(HTTPS_KEYS)); break;
        case Module::MODBUS_TCP: unknown = unknownKey(args, MODBUS_KEYS, COUNT_OF(MODBUS_KEYS)); break;
        case Module::TCP: unknown = unknownKey(args, TCP_KEYS, COUNT_OF(TCP_KEYS)); break;
        case Module::UDP: unknown = unknownKey(args, UDP_KEYS, COUNT_OF(UDP_KEYS)); break;
    }
    if (unknown != nullptr) return fail(v.error, BAD_JOB, job.id, unknown);
    switch (job.module) {
        case Module::HTTP:
        case Module::HTTPS: return parseHttpArgs(v, job, args, out);
        case Module::MODBUS_TCP: return parseModbusArgs(v, job, args, out, write);
        case Module::TCP:
        case Module::UDP: return parseRawArgs(v, job, args, out);
    }
    return fail(v.error, BAD_JOB, job.id, "module");
}

bool captureNameTaken(const Job &job, const char *name) {
    for (size_t i = 0; i < job.captureCount; i++) {
        if (strcmp(job.captures[i].name, name) == 0) return true;
    }
    return false;
}

bool parseCaptures(Validation &v, Job &job, JsonVariantConst value, bool login, size_t stepIndex) {
    if (!value.is<JsonArrayConst>()) return fail(v.error, BAD_JOB, job.id, "captures");
    JsonArrayConst array = value.as<JsonArrayConst>();
    if (job.captureCount + array.size() > CAPTURES_MAX) return fail(v.error, QUOTA_EXCEEDED, job.id, "captures");
    for (JsonVariantConst item : array) {
        if (!item.is<JsonObjectConst>()) return fail(v.error, BAD_JOB, job.id, "captures");
        JsonObjectConst object = item.as<JsonObjectConst>();
        const char *unknown = unknownKey(object, CAPTURE_KEYS, COUNT_OF(CAPTURE_KEYS));
        if (unknown != nullptr) return fail(v.error, BAD_JOB, job.id, unknown);

        Capture &capture = job.captures[job.captureCount];
        const char *text = nullptr;
        size_t length = 0;
        if (getString(object["name"], 1, ID_MAX, &text, &length) != Got::OK || !isId(text, length, true)) return fail(v.error, BAD_JOB, job.id, "name");
        memcpy(capture.name, text, length);
        capture.name[length] = '\0';
        if (captureNameTaken(job, capture.name)) return fail(v.error, BAD_JOB, job.id, "name");
        capture.login = login;
        capture.step = static_cast<uint8_t>(stepIndex);

        if (getString(object["from"], 1, 8, &text, &length) != Got::OK) return fail(v.error, BAD_JOB, job.id, "from");
        if (strcmp(text, "header") == 0) {
            capture.fromHeader = true;
        } else if (strcmp(text, "body") != 0) {
            return fail(v.error, BAD_JOB, job.id, "from");
        }

        static const char *const FIELDS[] = {"key", "header", "after", "before"};
        const char **targets[] = {&capture.key, &capture.header, &capture.after, &capture.before};
        const size_t maxLength[] = {CAPTURE_KEY_MAX, ID_MAX * 2, DELIMITER_MAX, DELIMITER_MAX};
        for (size_t f = 0; f < COUNT_OF(FIELDS); f++) {
            Got got = getString(object[FIELDS[f]], 1, maxLength[f], &text, &length);
            if (got == Got::BAD) return fail(v.error, BAD_JOB, job.id, FIELDS[f]);
            if (got == Got::OK && !storeString(v, job, text, length, targets[f])) return false;
        }
        if (capture.fromHeader) {
            if (capture.key != nullptr) return fail(v.error, BAD_JOB, job.id, "key");
            if (capture.header == nullptr || !GatewayLogic::isValidHeaderName(capture.header)) return fail(v.error, BAD_JOB, job.id, "header");
        } else {
            if (capture.header != nullptr) return fail(v.error, BAD_JOB, job.id, "header");
            if (capture.key != nullptr && (capture.after != nullptr || capture.before != nullptr))
                return fail(v.error, BAD_JOB, job.id, capture.after != nullptr ? "after" : "before");
            if (capture.key == nullptr && capture.after == nullptr) return fail(v.error, BAD_JOB, job.id, "after");
            if (capture.key == nullptr && capture.before == nullptr) return fail(v.error, BAD_JOB, job.id, "before");
        }
        job.captureCount++;
    }
    return true;
}

bool stepIdTaken(const Job &job, const char *id, bool login, size_t index) {
    for (size_t i = 0; i < job.loginCount && !(login && i == index); i++) {
        if (strcmp(job.login[i].id, id) == 0) return true;
    }
    if (login) return false;
    for (size_t i = 0; i < index; i++) {
        if (strcmp(job.steps[i].id, id) == 0) return true;
    }
    return false;
}

bool parseStep(Validation &v, size_t jobIndex, JsonVariantConst value, bool login, size_t index) {
    Job &job = v.table->jobs[jobIndex];
    Step &step = stepAt(job, login, index);
    if (!value.is<JsonObjectConst>()) return fail(v.error, BAD_JOB, job.id, login ? "login" : "steps");
    JsonObjectConst object = value.as<JsonObjectConst>();
    const char *text = nullptr;
    size_t length = 0;
    if (getString(object["id"], 1, ID_MAX, &text, &length) != Got::OK || !isId(text, length, false)) return fail(v.error, BAD_JOB, job.id, "id");
    memcpy(step.id, text, length);
    step.id[length] = '\0';
    if (stepIdTaken(job, step.id, login, index)) return fail(v.error, BAD_JOB, job.id, "id");

    const char *unknown = unknownKey(object, STEP_KEYS, COUNT_OF(STEP_KEYS));
    if (unknown != nullptr) return fail(v.error, BAD_JOB, job.id, unknown);

    int64_t number = 0;
    Got got = getInt(object["timeout_ms"], STEP_TIMEOUT_MIN_MS, STEP_TIMEOUT_MAX_MS, &number);
    if (got == Got::BAD) return fail(v.error, BAD_JOB, job.id, "timeout_ms");
    step.timeoutMs = got == Got::OK ? static_cast<uint32_t>(number) : job.stepTimeoutMs;
    got = getInt(object["max_bytes"], 1, RUN_BYTES_MAX, &number);
    if (got == Got::BAD) return fail(v.error, BAD_JOB, job.id, "max_bytes");
    step.maxBytes = got == Got::OK ? static_cast<uint32_t>(number) : job.maxBytes;

    if (object["args"].isNull()) return fail(v.error, BAD_JOB, job.id, "args");
    size_t slot = login ? index : LOGIN_MAX + index;
    if (!parseArgs(v, job, object["args"], step.args, &v.modbusWrite[jobIndex][slot])) return false;

    if (!object["captures"].isNull() && !parseCaptures(v, job, object["captures"], login, index)) return false;
    return true;
}

bool parseVars(Validation &v, Job &job, JsonVariantConst value) {
    if (!value.is<JsonObjectConst>()) return fail(v.error, BAD_JOB, job.id, "vars");
    JsonObjectConst object = value.as<JsonObjectConst>();
    if (object.size() > VARS_MAX) return fail(v.error, QUOTA_EXCEEDED, job.id, "vars");
    bool backfillSet = false;
    for (JsonPairConst pair : object) {
        const char *name = pair.key().c_str();
        size_t nameLength = strlen(name);
        if (!isId(name, nameLength, true)) return fail(v.error, BAD_JOB, job.id, "vars");
        Var &var = job.vars[job.varCount];
        memcpy(var.name, name, nameLength);
        var.name[nameLength] = '\0';

        struct Reserved {
            const char *name;
            int64_t min, max;
        };
        static const Reserved RESERVED[] = {{"clock_offset_s", -CLOCK_OFFSET_MAX_S, CLOCK_OFFSET_MAX_S},
                                            {"overlap_s", 0, OVERLAP_MAX_S},
                                            {"backfill_s", 0, BACKFILL_MAX_S},
                                            {"window_max_s", WINDOW_MIN_S, INTERVAL_MAX_S}};
        const Reserved *reserved = nullptr;
        for (const Reserved &r : RESERVED) {
            if (strcmp(r.name, name) == 0) reserved = &r;
        }

        JsonVariantConst item = pair.value();
        char number[24];
        const char *text = nullptr;
        size_t length = 0;
        if (item.is<int64_t>()) {
            int64_t x = item.as<int64_t>();
            if (reserved != nullptr && (x < reserved->min || x > reserved->max)) return fail(v.error, BAD_JOB, job.id, name);
            if (reserved == &RESERVED[0]) job.clockOffsetS = x;
            if (reserved == &RESERVED[1]) job.overlapS = static_cast<uint32_t>(x);
            if (reserved == &RESERVED[2]) {
                job.backfillS = static_cast<uint32_t>(x);
                backfillSet = true;
            }
            if (reserved == &RESERVED[3]) job.windowMaxS = static_cast<uint32_t>(x);
            snprintf(number, sizeof(number), "%lld", static_cast<long long>(x));
            text = number;
            length = strlen(number);
        } else {
            if (reserved != nullptr) return fail(v.error, BAD_JOB, job.id, name);
            if (!item.is<const char *>()) return fail(v.error, BAD_JOB, job.id, "vars");
            Got got = getString(item, 0, VAR_VALUE_MAX, &text, &length);
            if (got != Got::OK) {
                bool tooLong = item.as<JsonString>().size() > VAR_VALUE_MAX;
                return fail(v.error, tooLong ? QUOTA_EXCEEDED : BAD_JOB, job.id, "vars");
            }
        }
        if (!storeString(v, job, text, length, &var.text)) return false;
        job.varCount++;
    }
    if (backfillSet && job.backfillS < job.overlapS) return fail(v.error, BAD_JOB, job.id, "backfill_s");
    if (!backfillSet && job.backfillS < job.overlapS) job.backfillS = job.overlapS;
    return true;
}

bool parseSession(Validation &v, size_t jobIndex, JsonVariantConst value) {
    Job &job = v.table->jobs[jobIndex];
    if (!value.is<JsonObjectConst>()) return fail(v.error, BAD_JOB, job.id, "session");
    JsonObjectConst object = value.as<JsonObjectConst>();
    const char *unknown = unknownKey(object, SESSION_KEYS, COUNT_OF(SESSION_KEYS));
    if (unknown != nullptr) return fail(v.error, BAD_JOB, job.id, unknown);
    job.hasSession = true;

    int64_t ttl = 0;
    if (getInt(object["ttl_s"], TTL_MIN_S, TTL_MAX_S, &ttl) != Got::OK) return fail(v.error, BAD_JOB, job.id, "ttl_s");
    job.ttlS = static_cast<uint32_t>(ttl);

    JsonVariantConst expiry = object["expiry"];
    if (!expiry.isNull()) {
        if (!expiry.is<JsonObjectConst>()) return fail(v.error, BAD_JOB, job.id, "expiry");
        JsonObjectConst e = expiry.as<JsonObjectConst>();
        unknown = unknownKey(e, EXPIRY_KEYS, COUNT_OF(EXPIRY_KEYS));
        if (unknown != nullptr) return fail(v.error, BAD_JOB, job.id, unknown);
        JsonVariantConst status = e["status"];
        if (!status.isNull()) {
            if (!status.is<JsonArrayConst>()) return fail(v.error, BAD_JOB, job.id, "status");
            JsonArrayConst array = status.as<JsonArrayConst>();
            if (array.size() > EXPIRY_STATUS_MAX) return fail(v.error, QUOTA_EXCEEDED, job.id, "status");
            for (JsonVariantConst item : array) {
                int64_t code = 0;
                if (getInt(item, 100, 599, &code) != Got::OK) return fail(v.error, BAD_JOB, job.id, "status");
                job.expiryStatus[job.expiryStatusCount++] = static_cast<uint16_t>(code);
            }
        }
        const char *text = nullptr;
        size_t length = 0;
        Got got = getString(e["marker"], 1, DELIMITER_MAX, &text, &length);
        if (got == Got::BAD) return fail(v.error, BAD_JOB, job.id, "marker");
        if (got == Got::OK && !storeString(v, job, text, length, &job.expiryMarker)) return false;
    }

    JsonVariantConst login = object["login"];
    if (!login.is<JsonArrayConst>() || login.as<JsonArrayConst>().size() == 0) return fail(v.error, BAD_JOB, job.id, "login");
    JsonArrayConst array = login.as<JsonArrayConst>();
    if (array.size() > LOGIN_MAX) return fail(v.error, QUOTA_EXCEEDED, job.id, "login");
    for (JsonVariantConst item : array) {
        if (!parseStep(v, jobIndex, item, true, job.loginCount)) return false;
        job.loginCount++;
    }
    return true;
}

bool parseJob(Validation &v, size_t index, JsonVariantConst value) {
    Job &job = v.table->jobs[index];
    if (!value.is<JsonObjectConst>()) return fail(v.error, BAD_JOB, nullptr, "jobs");
    JsonObjectConst object = value.as<JsonObjectConst>();

    const char *text = nullptr;
    size_t length = 0;
    if (getString(object["id"], 1, ID_MAX, &text, &length) != Got::OK || !isId(text, length, false)) return fail(v.error, BAD_JOB, nullptr, "id");
    memcpy(job.id, text, length);
    job.id[length] = '\0';
    for (size_t k = 0; k < index; k++) {
        if (strcmp(v.table->jobs[k].id, job.id) == 0) return fail(v.error, BAD_JOB, job.id, "id");
    }
    const char *unknown = unknownKey(object, JOB_KEYS, COUNT_OF(JOB_KEYS));
    if (unknown != nullptr) return fail(v.error, BAD_JOB, job.id, unknown);

    int64_t number = 0;
    if (getInt(object["rev"], 1, UINT32_MAX, &number) != Got::OK) return fail(v.error, BAD_JOB, job.id, "rev");
    job.rev = static_cast<uint32_t>(number);
    if (!object["enabled"].is<bool>()) return fail(v.error, BAD_JOB, job.id, "enabled");
    job.enabled = object["enabled"].as<bool>();
    if (getString(object["source_id"], 1, SOURCE_ID_MAX, &text, &length) != Got::OK || !isSourceId(text, length))
        return fail(v.error, BAD_JOB, job.id, "source_id");
    memcpy(job.sourceId, text, length);
    job.sourceId[length] = '\0';

    JsonVariantConst target = object["target"];
    if (!target.is<JsonObjectConst>()) return fail(v.error, BAD_JOB, job.id, "target");
    unknown = unknownKey(target.as<JsonObjectConst>(), TARGET_KEYS, COUNT_OF(TARGET_KEYS));
    if (unknown != nullptr) return fail(v.error, BAD_JOB, job.id, unknown);
    if (getString(target["ip"], 7, 15, &text, &length) != Got::OK || !GatewayLogic::parseIpv4(text, job.octets))
        return fail(v.error, BAD_JOB, job.id, "target");
    memcpy(job.ip, text, length);
    job.ip[length] = '\0';
    if (getInt(target["port"], 1, 65535, &number) != Got::OK) return fail(v.error, BAD_JOB, job.id, "target");
    job.port = static_cast<uint16_t>(number);

    if (getString(object["module"], 1, 16, &text, &length) != Got::OK || !GatewayLogic::parseModule(text, &job.module))
        return fail(v.error, BAD_JOB, job.id, "module");

    if (getInt(object["interval_s"], INTERVAL_MIN_S, INTERVAL_MAX_S, &number) != Got::OK) return fail(v.error, BAD_JOB, job.id, "interval_s");
    job.intervalS = static_cast<uint32_t>(number);
    int64_t half = job.intervalS / 2;
    Got got = getInt(object["jitter_s"], 0, half < JITTER_MAX_S ? half : JITTER_MAX_S, &number);
    if (got == Got::BAD) return fail(v.error, BAD_JOB, job.id, "jitter_s");
    job.jitterS = got == Got::OK ? static_cast<uint32_t>(number) : 0;
    got = getInt(object["step_timeout_ms"], STEP_TIMEOUT_MIN_MS, STEP_TIMEOUT_MAX_MS, &number);
    if (got == Got::BAD) return fail(v.error, BAD_JOB, job.id, "step_timeout_ms");
    job.stepTimeoutMs = static_cast<uint32_t>(got == Got::OK ? number : STEP_TIMEOUT_DEFAULT_MS);
    got = getInt(object["deadline_s"], DEADLINE_MIN_S, half < DEADLINE_MAX_S ? half : DEADLINE_MAX_S, &number);
    if (got == Got::BAD) return fail(v.error, BAD_JOB, job.id, "deadline_s");
    job.deadlineS = static_cast<uint32_t>(got == Got::OK ? number : (half < DEADLINE_DEFAULT_S ? half : DEADLINE_DEFAULT_S));
    got = getInt(object["max_bytes"], 1, RUN_BYTES_MAX, &number);
    if (got == Got::BAD) return fail(v.error, BAD_JOB, job.id, "max_bytes");
    job.maxBytes = static_cast<uint32_t>(got == Got::OK ? number : MAX_BYTES_DEFAULT);

    JsonVariantConst publish = object["publish"];
    if (!publish.isNull()) {
        if (getString(publish, 1, 16, &text, &length) != Got::OK) return fail(v.error, BAD_JOB, job.id, "publish");
        if (strcmp(text, "batch") == 0) {
            job.batch = true;
        } else if (strcmp(text, "per_step") != 0) {
            return fail(v.error, BAD_JOB, job.id, "publish");
        }
    }

    job.backfillS = BACKFILL_DEFAULT_S;
    job.windowMaxS = WINDOW_MAX_DEFAULT_S;
    if (!object["vars"].isNull() && !parseVars(v, job, object["vars"])) return false;
    if (!object["session"].isNull() && !parseSession(v, index, object["session"])) return false;

    JsonVariantConst steps = object["steps"];
    if (!steps.is<JsonArrayConst>() || steps.as<JsonArrayConst>().size() == 0) return fail(v.error, BAD_JOB, job.id, "steps");
    JsonArrayConst array = steps.as<JsonArrayConst>();
    if (array.size() > STEPS_MAX) return fail(v.error, QUOTA_EXCEEDED, job.id, "steps");
    uint32_t runBytes = 0;
    for (JsonVariantConst item : array) {
        if (!parseStep(v, index, item, false, job.stepCount)) return false;
        runBytes += job.steps[job.stepCount].maxBytes;
        job.stepCount++;
    }
    if (runBytes > RUN_BYTES_MAX) return fail(v.error, QUOTA_EXCEEDED, job.id, "max_bytes");

    for (size_t k = 0; k < index; k++) {
        const Job &other = v.table->jobs[k];
        if (strcmp(other.sourceId, job.sourceId) == 0 &&
            (memcmp(other.octets, job.octets, 4) != 0 || other.port != job.port || other.module != job.module))
            return fail(v.error, BAD_JOB, job.id, "source_id");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Stage 2: read-only classing
// ---------------------------------------------------------------------------

bool isRead(Validation &v, const Job &job, const Step &step, bool modbusWrite) {
    GatewayLogic::PolicyRequest request{};
    request.module = job.module;
    memcpy(request.ip, job.octets, 4);
    request.port = job.port;
    request.access = GatewayLogic::Access::NONE;
    uint8_t digest[GatewayLogic::DIGEST_BYTES];
    const StepArgs &args = step.args;
    switch (job.module) {
        case Module::HTTP:
        case Module::HTTPS:
            // No exact declaration can match a path that expands differently each run.
            if (strcmp(args.method, "GET") != 0 && GatewayLogic::hasPlaceholder(args.path, strlen(args.path))) return false;
            request.method = args.method;
            request.path = args.path;
            request.hasBody = (args.body != nullptr && args.body[0] != '\0') || args.bodyBytesLength > 0;
            break;
        case Module::MODBUS_TCP:
            if (modbusWrite) return false;
            request.function = GatewayLogic::functionFor(args.address.table, false, args.count, false);
            request.modbus = args.address;
            break;
        case Module::TCP:
        case Module::UDP: {
            const uint8_t *bytes = args.payloadBytes;
            size_t length = args.payloadBytesLength;
            if (args.payload != nullptr) {
                if (GatewayLogic::hasPlaceholder(args.payload, strlen(args.payload))) return false;
                bytes = reinterpret_cast<const uint8_t *>(args.payload);
                length = strlen(args.payload);
            }
            request.payloadLength = length;
            if (length > 0) {
                if (v.context.sha256 == nullptr) return false;
                v.context.sha256(bytes, length, digest);
                request.payloadDigest = digest;
            }
            break;
        }
    }
    int entry = -1;
    return GatewayLogic::classify(request, v.context.grants, v.context.grantCount, &entry) == GatewayLogic::Verdict::READ;
}

// ---------------------------------------------------------------------------
// Stage 4: templates
// ---------------------------------------------------------------------------

struct Placeholder {
    char name[GatewayLogic::TEMPLATE_NAME_MAX + 1];
    char arg[GatewayLogic::TEMPLATE_ARG_MAX + 1];
    EncodingChain chain;
    size_t length;
};

bool isNameChar(char c) { return (c >= 'a' && c <= 'z') || c == '_'; }

bool isArgChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
}

bool startsWith(const char *p, const char *end, const char *word) {
    size_t n = strlen(word);
    return static_cast<size_t>(end - p) >= n && memcmp(p, word, n) == 0;
}

// The grammar of GatewayLogic's parsePlaceholder: `${name[:arg][|enc]...}`.
bool parsePlaceholder(const char *p, const char *end, Placeholder *out) {
    const char *s = p + 2;
    size_t n = 0;
    while (s < end && isNameChar(*s)) {
        if (n >= GatewayLogic::TEMPLATE_NAME_MAX) return false;
        out->name[n++] = *s++;
    }
    if (n == 0) return false;
    out->name[n] = '\0';
    out->arg[0] = '\0';
    if (s < end && *s == ':') {
        s++;
        n = 0;
        while (s < end && isArgChar(*s)) {
            if (n >= GatewayLogic::TEMPLATE_ARG_MAX) return false;
            out->arg[n++] = *s++;
        }
        if (n == 0) return false;
        out->arg[n] = '\0';
    }
    out->chain.count = 0;
    while (s < end && *s == '|') {
        if (out->chain.count >= GatewayLogic::TEMPLATE_MAX_ENCODINGS) return false;
        s++;
        Encoding encoding;
        if (startsWith(s, end, "url")) {
            encoding = Encoding::URL;
            s += 3;
        } else if (startsWith(s, end, "json")) {
            encoding = Encoding::JSON;
            s += 4;
        } else if (startsWith(s, end, "b64")) {
            encoding = Encoding::B64;
            s += 3;
        } else {
            return false;
        }
        out->chain.encodings[out->chain.count++] = encoding;
    }
    if (s >= end || *s != '}') return false;
    out->length = static_cast<size_t>(s - p) + 1;
    return true;
}

bool sameChain(const EncodingChain &a, const EncodingChain &b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (a.encodings[i] != b.encodings[i]) return false;
    }
    return true;
}

// A capture is usable by a step when an earlier step captured it: login
// captures by later login steps and every data step, data captures by later data steps.
bool captureAvailable(const Capture &capture, bool login, size_t index) {
    if (capture.login) return !login || capture.step < index;
    return !login && capture.step < index;
}

bool varExists(const Job &job, const char *name) {
    for (size_t i = 0; i < job.varCount; i++) {
        if (strcmp(job.vars[i].name, name) == 0) return true;
    }
    return false;
}

bool checkTemplate(Job &job, const char *text, bool login, size_t index) {
    if (text == nullptr) return true;
    const char *p = text;
    const char *end = text + strlen(text);
    while (p < end) {
        if (*p != '$') {
            p++;
            continue;
        }
        if (end - p >= 3 && p[1] == '$' && p[2] == '{') { // escape, literal `${`
            p += 3;
            continue;
        }
        if (end - p < 2 || p[1] != '{') { // a lone '$'
            p++;
            continue;
        }
        Placeholder placeholder{};
        if (!parsePlaceholder(p, end, &placeholder)) return false;
        const char *name = placeholder.name;
        const char *arg = placeholder.arg;
        if (strcmp(name, "now") == 0 || strcmp(name, "cursor") == 0) {
            if (arg[0] != '\0' && strcmp(arg, "ms") != 0) return false;
            job.usesTime = true;
            if (name[0] == 'c') job.usesCursor = true;
        } else if (strcmp(name, "var") == 0) {
            if (!varExists(job, arg)) return false;
        } else if (strcmp(name, "cap") == 0) {
            Capture *capture = nullptr;
            for (size_t c = 0; c < job.captureCount; c++) {
                if (strcmp(job.captures[c].name, arg) == 0) capture = &job.captures[c];
            }
            if (capture == nullptr || !captureAvailable(*capture, login, index)) return false;
            bool known = false;
            for (size_t c = 0; c < capture->chainCount; c++) {
                known = known || sameChain(capture->chains[c], placeholder.chain);
            }
            if (!known) {
                if (capture->chainCount >= CHAINS_PER_CAPTURE) return false;
                capture->chains[capture->chainCount++] = placeholder.chain;
            }
        } else if (strcmp(name, "secret") != 0 || arg[0] == '\0') {
            return false;
        }
        p += placeholder.length;
    }
    return true;
}

bool checkStepTemplates(Job &job, const Step &step, bool login, size_t index) {
    const StepArgs &args = step.args;
    if (!checkTemplate(job, args.path, login, index) || !checkTemplate(job, args.body, login, index) ||
        !checkTemplate(job, args.payload, login, index))
        return false;
    for (size_t h = 0; h < args.headerCount; h++) {
        if (!checkTemplate(job, args.headers[h].value, login, index)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Binding canonical form
// ---------------------------------------------------------------------------

struct Writer {
    char *out;
    size_t cap;
    size_t used;
    bool ok;

    void raw(const void *data, size_t length) {
        if (!ok || length > cap - used) {
            ok = false;
            return;
        }
        memcpy(out + used, data, length);
        used += length;
    }
    void text(const char *s) { raw(s, strlen(s)); }
    void number(int64_t value) {
        char buffer[24];
        int n = snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
        raw(buffer, static_cast<size_t>(n));
    }
    // Length-prefixed bytes; `~` for an absent value.
    void bytes(const void *data, size_t length, bool present) {
        if (!present) {
            raw("~", 1);
            return;
        }
        number(static_cast<int64_t>(length));
        raw(":", 1);
        raw(data, length);
    }
    void string(const char *s) { bytes(s, s != nullptr ? strlen(s) : 0, s != nullptr); }
    void field(const char *tag, int64_t value) {
        text(tag);
        number(value);
        raw(";", 1);
    }
};

void writeStep(Writer &w, const Job &job, const Step &step, size_t index) {
    const StepArgs &a = step.args;
    w.text("step=");
    w.string(step.id);
    w.text(";m=");
    w.string(a.method);
    w.text(";p=");
    w.string(a.path);
    w.field(";h=", a.headerCount);
    for (size_t h = 0; h < a.headerCount; h++) {
        w.string(a.headers[h].name);
        w.string(a.headers[h].value);
    }
    w.text(";b=");
    w.string(a.body);
    w.text(";bb=");
    w.bytes(a.bodyBytes, a.bodyBytesLength, a.bodyBytes != nullptr);
    w.text(";c=");
    w.string(a.certSha256);
    w.field(";u=", a.address.unit);
    w.field("t=", static_cast<int64_t>(a.address.table));
    w.field("a=", a.address.address);
    w.field("n=", a.count);
    w.text("pl=");
    w.string(a.payload);
    w.text(";pb=");
    w.bytes(a.payloadBytes, a.payloadBytesLength, a.payloadBytes != nullptr);
    w.field(";i=", a.idleMs);
    w.field("w=", a.waitMs);
    for (size_t c = 0; c < job.captureCount; c++) {
        const Capture &capture = job.captures[c];
        if (!capture.login || capture.step != index) continue;
        w.text("cap=");
        w.string(capture.name);
        w.text(capture.fromHeader ? "h" : "b");
        w.string(capture.key);
        w.string(capture.header);
        w.string(capture.after);
        w.string(capture.before);
        w.raw(";", 1);
    }
}

bool run(JsonDocument &doc, const char *text, size_t length, const ValidationContext &context, JobTable *table, ApplyError *error) {
    DeserializationError parsed = deserializeJson(doc, text, length);
    if (parsed || !doc.is<JsonObjectConst>()) return fail(error, BAD_JSON, nullptr, nullptr);
    JsonObjectConst root = doc.as<JsonObjectConst>();

    const char *unknown = unknownKey(root, TOP_KEYS, COUNT_OF(TOP_KEYS));
    if (unknown != nullptr) return fail(error, BAD_JOB, nullptr, unknown);
    int64_t number = 0;
    if (getInt(root["v"], 1, 1, &number) != Got::OK) return fail(error, BAD_JOB, nullptr, "v");
    Got got = getInt(root["budget_bytes_h"], BUDGET_MIN, BUDGET_MAX, &number);
    if (got == Got::BAD) return fail(error, BAD_JOB, nullptr, "budget_bytes_h");
    table->budgetBytesH = got == Got::OK ? static_cast<uint32_t>(number) : BUDGET_DEFAULT;
    JsonVariantConst jobs = root["jobs"];
    if (!jobs.is<JsonArrayConst>()) return fail(error, BAD_JOB, nullptr, "jobs");
    JsonArrayConst array = jobs.as<JsonArrayConst>();
    if (array.size() > JOBS_MAX) return fail(error, QUOTA_EXCEEDED, nullptr, "jobs");

    Validation v{context, table, error, {}};
    for (JsonVariantConst item : array) {
        if (!parseJob(v, table->jobCount, item)) return false;
        table->jobCount++;
    }

    if (context.classify) {
        for (size_t j = 0; j < table->jobCount; j++) {
            const Job &job = table->jobs[j];
            for (size_t i = 0; i < job.loginCount; i++) {
                if (!isRead(v, job, job.login[i], v.modbusWrite[j][i])) return failStep(error, WRITE_NOT_ALLOWED, job.id, true, i);
            }
            for (size_t i = 0; i < job.stepCount; i++) {
                if (!isRead(v, job, job.steps[i], v.modbusWrite[j][LOGIN_MAX + i])) return failStep(error, WRITE_NOT_ALLOWED, job.id, false, i);
            }
        }
    }

    for (size_t j = 0; j < table->jobCount; j++) {
        const Job &job = table->jobs[j];
        int verdict = context.targetCheck != nullptr ? context.targetCheck(job.octets) : (GatewayLogic::isPrivateIpv4(job.octets) ? 0 : 1);
        if (verdict == 1) return fail(error, TARGET_NOT_PRIVATE, job.id, "target");
        if (verdict != 0) return fail(error, TARGET_NOT_ALLOWED, job.id, "target");
    }

    for (size_t j = 0; j < table->jobCount; j++) {
        Job &job = table->jobs[j];
        for (size_t i = 0; i < job.loginCount; i++) {
            if (!checkStepTemplates(job, job.login[i], true, i)) return failStep(error, BAD_TEMPLATE, job.id, true, i);
        }
        for (size_t i = 0; i < job.stepCount; i++) {
            if (!checkStepTemplates(job, job.steps[i], false, i)) return failStep(error, BAD_TEMPLATE, job.id, false, i);
        }
    }

    for (size_t j = 0; j < table->jobCount; j++) {
        const Job &job = table->jobs[j];
        if (job.module == Module::HTTPS && !context.httpsAvailable) return fail(error, MODULE_UNAVAILABLE, job.id, "module");
    }

    // The canonical binding is written into the unused pool tail and hashed there.
    for (size_t j = 0; j < table->jobCount; j++) {
        Job &job = table->jobs[j];
        char *scratch = table->pool + table->poolUsed;
        size_t written = canonicalBinding(job, scratch, table->poolCap - table->poolUsed);
        if (written == 0) return fail(error, QUOTA_EXCEEDED, job.id, "doc");
        if (context.sha256 != nullptr) context.sha256(reinterpret_cast<const uint8_t *>(scratch), written, job.bindingHash);
        memset(scratch, 0, written);
    }
    if (context.sha256 != nullptr) context.sha256(reinterpret_cast<const uint8_t *>(text), length, table->sha256);
    return true;
}

} // namespace

size_t canonicalBinding(const Job &job, char *out, size_t cap) {
    Writer w{out, cap, 0, out != nullptr};
    w.text("ip=");
    w.text(job.ip);
    w.field(";port=", job.port);
    w.text("mod=");
    w.text(GatewayLogic::moduleName(job.module));
    w.field(";session=", job.hasSession ? 1 : 0);
    if (job.hasSession) {
        w.field("ttl=", job.ttlS);
        w.field("status=", job.expiryStatusCount);
        for (size_t i = 0; i < job.expiryStatusCount; i++) w.field(",", job.expiryStatus[i]);
        w.text("marker=");
        w.string(job.expiryMarker);
        w.field(";login=", job.loginCount);
        for (size_t i = 0; i < job.loginCount; i++) writeStep(w, job, job.login[i], i);
    }
    return w.ok ? w.used : 0;
}

bool validateJobSet(const char *doc, size_t length, const ValidationContext &context, JobTable *out, ApplyError *error) {
    error->code = nullptr;
    error->job[0] = '\0';
    error->field[0] = '\0';
    char *pool = out->pool;
    size_t poolCap = out->poolCap;
    memset(out, 0, sizeof(*out));
    out->pool = pool;
    out->poolCap = poolCap;
    if (pool == nullptr || doc == nullptr) return fail(error, BAD_JSON, nullptr, nullptr);
    if (context.allocator != nullptr) {
        JsonDocument json(context.allocator);
        return run(json, doc, length, context, out, error);
    }
    JsonDocument json;
    return run(json, doc, length, context, out, error);
}

} // namespace CollectorLogic
