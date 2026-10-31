// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

// Unit tests for collector_logic: job set validation (schema, quotas,
// read-only classing, targets, templates, modules), binding hash, captures,
// encoding chains, expiry and the capture echo scrub.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>

#include "collector_logic.h"
#include "gateway_template.h"

using namespace CollectorLogic;
using GatewayLogic::Module;

void setUp(void) {}
void tearDown(void) {}

namespace {

// Deterministic 32-byte digest stub (not SHA-256; the logic only compares digests).
void stubSha(const uint8_t *data, size_t length, uint8_t out[32]) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < length; i++) h = (h ^ data[i]) * 1099511628211ULL;
    for (size_t i = 0; i < 32; i++) {
        h = (h ^ i) * 1099511628211ULL;
        out[i] = static_cast<uint8_t>(h >> 56);
    }
}

int targetVerdict = 0;
int targetCheck(const uint8_t[4]) { return targetVerdict; }

char poolStorage[POOL_BYTES];
JobTable table; // static: the table is large
ApplyError error;
bool httpsAvailable = true;

bool validate(const std::string &doc) {
    ValidationContext context{stubSha, targetCheck, httpsAvailable, nullptr};
    table.pool = poolStorage;
    table.poolCap = sizeof(poolStorage);
    return validateJobSet(doc.c_str(), doc.size(), context, &table, &error);
}

void expectError(const std::string &doc, const char *code, const char *job, const char *field) {
    bool ok = validate(doc);
    char message[200];
    snprintf(message, sizeof(message), "got %s job=%s field=%s", error.code ? error.code : "(ok)", error.job, error.field);
    TEST_ASSERT_FALSE_MESSAGE(ok, message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(code, error.code, message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(job, error.job, message);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(field, error.field, message);
}

void expectValid(const std::string &doc) {
    bool ok = validate(doc);
    char message[200];
    snprintf(message, sizeof(message), "got %s job=%s field=%s", error.code ? error.code : "(ok)", error.job, error.field);
    TEST_ASSERT_TRUE_MESSAGE(ok, message);
    TEST_ASSERT_NULL(error.code);
}

// Design worked example 1 (Dixell XWEB history).
const char *const XWEB_JOB = R"({
 "id":"xweb_hist","rev":1,"enabled":true,"source_id":"src_7f3a",
 "target":{"ip":"192.168.1.50","port":80},"module":"http",
 "interval_s":300,"jitter_s":30,"step_timeout_ms":8000,"deadline_s":60,"max_bytes":131072,
 "vars":{"overlap_s":900,"backfill_s":21600,"window_max_s":3600,"user":"energyme",
   "req":"{\"13\":[1,4],\"14\":[1,4],\"15\":[1,4]}"},
 "session":{"ttl_s":1800,"expiry":{"status":[401,403],"marker":"login.lua"},
   "login":[{"id":"login","args":{"method":"POST","path":"/cgi-bin/login.lua",
     "headers":{"Content-Type":"application/x-www-form-urlencoded"},
     "body":"user=${var:user|url}&pass=${secret:xweb_pw|url}"},
     "captures":[{"name":"sses","from":"body","after":"sses=","before":"&"}]}]},
 "steps":[{"id":"chart","args":{"method":"POST","path":"/cgi-bin/datachart.cgi",
   "headers":{"Content-Type":"application/x-www-form-urlencoded"},
   "body":"SSES=${cap:sses|url}&source=circular&startTS=${cursor}&stopTS=${now}&request=${var:req|url}"}}],
 "publish":"per_step"})";

// Design worked example 2 (Modbus TCP gateway with three meters).
const char *const MODBUS_JOB = R"({"id":"mb_meters","rev":1,"enabled":true,"source_id":"src_9c1d",
 "target":{"ip":"192.168.1.60","port":502},"module":"modbus_tcp",
 "interval_s":60,"jitter_s":5,"step_timeout_ms":1000,"deadline_s":20,"max_bytes":256,
 "steps":[{"id":"u3","args":{"address":"u3/hr3000","count":60}},
          {"id":"u4","args":{"address":"u4/hr3000","count":60}},
          {"id":"u5","args":{"address":"u5/ir0","count":40}}],
 "publish":"batch"})";

std::string set(const std::string &jobs) { return std::string(R"({"v":1,"budget_bytes_h":2097152,"jobs":[)") + jobs + "]}"; }

std::string replace(std::string text, const std::string &from, const std::string &to) {
    size_t at = text.find(from);
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, from.c_str());
    return text.replace(at, from.size(), to);
}

// A minimal job with one step, `args` and `module` substituted.
std::string job(const char *id, const char *module, const char *ip, int port, const std::string &steps, const std::string &extra = "") {
    char head[300];
    snprintf(head, sizeof(head), R"({"id":"%s","rev":1,"enabled":true,"source_id":"src_%04x","target":{"ip":"%s","port":%d},"module":"%s","interval_s":60)",
             id, static_cast<unsigned>(std::hash<std::string>{}(id) & 0xFFFF), ip, port, module);
    return std::string(head) + extra + R"(,"steps":)" + steps + "}";
}

std::string httpStep(const char *method, const char *path, const std::string &more = "") {
    return std::string(R"([{"id":"s1","args":{"method":")") + method + R"(","path":")" + path + "\"" + more + "}}]";
}

void reset() {
    targetVerdict = 0;
    httpsAvailable = true;
    memset(&error, 0, sizeof(error));
}

} // namespace

// ---------------------------------------------------------------------------
// Worked examples
// ---------------------------------------------------------------------------

static void test_xweb_example(void) {
    reset();
    expectValid(set(XWEB_JOB));
    TEST_ASSERT_EQUAL_UINT8(1, table.jobCount);
    TEST_ASSERT_EQUAL_UINT32(2097152, table.budgetBytesH);
    const Job &j = table.jobs[0];
    TEST_ASSERT_EQUAL_STRING("xweb_hist", j.id);
    TEST_ASSERT_EQUAL_STRING("src_7f3a", j.sourceId);
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", j.ip);
    TEST_ASSERT_EQUAL_UINT16(80, j.port);
    TEST_ASSERT_TRUE(j.module == Module::HTTP);
    TEST_ASSERT_EQUAL_UINT32(300, j.intervalS);
    TEST_ASSERT_EQUAL_UINT32(30, j.jitterS);
    TEST_ASSERT_EQUAL_UINT32(60, j.deadlineS);
    TEST_ASSERT_EQUAL_UINT32(900, j.overlapS);
    TEST_ASSERT_EQUAL_UINT32(21600, j.backfillS);
    TEST_ASSERT_EQUAL_UINT32(3600, j.windowMaxS);
    TEST_ASSERT_EQUAL_UINT8(5, j.varCount);
    TEST_ASSERT_EQUAL_STRING("900", j.vars[0].text);
    TEST_ASSERT_EQUAL_STRING("energyme", j.vars[3].text);
    TEST_ASSERT_EQUAL_STRING("{\"13\":[1,4],\"14\":[1,4],\"15\":[1,4]}", j.vars[4].text);
    TEST_ASSERT_TRUE(j.hasSession);
    TEST_ASSERT_EQUAL_UINT32(1800, j.ttlS);
    TEST_ASSERT_EQUAL_UINT8(2, j.expiryStatusCount);
    TEST_ASSERT_EQUAL_UINT16(403, j.expiryStatus[1]);
    TEST_ASSERT_EQUAL_STRING("login.lua", j.expiryMarker);
    TEST_ASSERT_EQUAL_UINT8(1, j.loginCount);
    TEST_ASSERT_EQUAL_STRING("POST", j.login[0].args.method);
    TEST_ASSERT_EQUAL_UINT32(8000, j.login[0].timeoutMs);
    TEST_ASSERT_EQUAL_UINT8(1, j.login[0].args.headerCount);
    TEST_ASSERT_EQUAL_STRING("Content-Type", j.login[0].args.headers[0].name);
    TEST_ASSERT_EQUAL_UINT8(1, j.stepCount);
    TEST_ASSERT_EQUAL_STRING("/cgi-bin/datachart.cgi", j.steps[0].args.path);
    TEST_ASSERT_EQUAL_UINT32(131072, j.steps[0].maxBytes);
    TEST_ASSERT_EQUAL_UINT8(1, j.captureCount);
    const Capture &c = j.captures[0];
    TEST_ASSERT_EQUAL_STRING("sses", c.name);
    TEST_ASSERT_TRUE(c.login);
    TEST_ASSERT_FALSE(c.fromHeader);
    TEST_ASSERT_EQUAL_STRING("sses=", c.after);
    TEST_ASSERT_EQUAL_STRING("&", c.before);
    TEST_ASSERT_EQUAL_UINT8(1, c.chainCount);
    TEST_ASSERT_EQUAL_UINT8(1, c.chains[0].count);
    TEST_ASSERT_TRUE(c.chains[0].encodings[0] == Encoding::URL);
    TEST_ASSERT_TRUE(j.usesTime);
    TEST_ASSERT_TRUE(j.usesCursor);
    TEST_ASSERT_FALSE(j.batch);
    uint8_t zero[32] = {};
    TEST_ASSERT_TRUE(memcmp(zero, j.bindingHash, 32) != 0);
    TEST_ASSERT_TRUE(memcmp(zero, table.sha256, 32) != 0);
    TEST_ASSERT_TRUE(table.poolUsed < POOL_BYTES);
}

static void test_modbus_example(void) {
    reset();
    expectValid(set(MODBUS_JOB));
    const Job &j = table.jobs[0];
    TEST_ASSERT_TRUE(j.module == Module::MODBUS_TCP);
    TEST_ASSERT_TRUE(j.batch);
    TEST_ASSERT_FALSE(j.usesTime);
    TEST_ASSERT_FALSE(j.hasSession);
    TEST_ASSERT_EQUAL_UINT8(3, j.stepCount);
    TEST_ASSERT_EQUAL_UINT8(3, j.steps[0].args.address.unit);
    TEST_ASSERT_TRUE(j.steps[2].args.address.table == GatewayLogic::ModbusTable::INPUT_REGISTERS);
    TEST_ASSERT_EQUAL_UINT16(40, j.steps[2].args.count);
    TEST_ASSERT_EQUAL_UINT32(256, j.steps[1].maxBytes);
    TEST_ASSERT_EQUAL_UINT32(1000, j.steps[1].timeoutMs);
    TEST_ASSERT_EQUAL_UINT32(3600, j.backfillS);
}

static void test_both_examples_in_one_set(void) {
    reset();
    std::string doc = set(std::string(XWEB_JOB) + "," + MODBUS_JOB);
    TEST_ASSERT_TRUE(doc.size() <= DOC_MAX);
    expectValid(doc);
    TEST_ASSERT_EQUAL_UINT8(2, table.jobCount);
    char message[96];
    snprintf(message, sizeof(message), "doc %u B, pool %u B, sizeof(JobTable) %u B", static_cast<unsigned>(doc.size()),
             static_cast<unsigned>(table.poolUsed), static_cast<unsigned>(sizeof(JobTable)));
    TEST_MESSAGE(message);
}

static void test_defaults_applied(void) {
    reset();
    expectValid(set(job("d", "modbus_tcp", "192.168.1.60", 502, R"([{"id":"u1","args":{"address":"u1/hr0","count":2}}])")));
    const Job &j = table.jobs[0];
    TEST_ASSERT_EQUAL_UINT32(0, j.jitterS);
    TEST_ASSERT_EQUAL_UINT32(5000, j.stepTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(30, j.deadlineS); // min(60, interval/2)
    TEST_ASSERT_EQUAL_UINT32(16384, j.maxBytes);
    TEST_ASSERT_EQUAL_UINT32(16384, j.steps[0].maxBytes);
    TEST_ASSERT_EQUAL_UINT32(5000, j.steps[0].timeoutMs);
    TEST_ASSERT_FALSE(j.batch);
    TEST_ASSERT_EQUAL_UINT32(3600, j.backfillS);
    TEST_ASSERT_EQUAL_UINT32(3600, j.windowMaxS);
    TEST_ASSERT_EQUAL_UINT32(0, j.overlapS);
    reset();
    expectValid(R"({"v":1,"jobs":[]})");
    TEST_ASSERT_EQUAL_UINT32(BUDGET_DEFAULT, table.budgetBytesH);
    TEST_ASSERT_EQUAL_UINT8(0, table.jobCount);
}

// ---------------------------------------------------------------------------
// Schema and quotas
// ---------------------------------------------------------------------------

static void test_empty_set_and_missing_v(void) {
    reset();
    expectValid(R"({"v":1,"jobs":[]})");
    expectError(R"({"jobs":[]})", "BAD_JOB", "", "v");
    expectError(R"({"v":2,"jobs":[]})", "BAD_JOB", "", "v");
    expectError(R"({"v":1})", "BAD_JOB", "", "jobs");
    expectError(R"({"v":1,"jobs":[],"extra":1})", "BAD_JOB", "", "extra");
    expectError(R"({"v":1,"budget_bytes_h":1000,"jobs":[]})", "BAD_JOB", "", "budget_bytes_h");
}

static void test_bad_json(void) {
    reset();
    expectError("not json", "BAD_JSON", "", "");
    expectError("[1,2]", "BAD_JSON", "", "");
    expectError(R"({"v":1,"jobs":[)", "BAD_JSON", "", "");
}

static void test_ninth_job_quota(void) {
    reset();
    std::string jobs;
    for (int i = 0; i < 9; i++) {
        char id[8];
        snprintf(id, sizeof(id), "j%d", i);
        if (i > 0) jobs += ",";
        jobs += job(id, "modbus_tcp", "192.168.1.60", 502, R"([{"id":"u1","args":{"address":"u1/hr0","count":2}}])");
    }
    expectError(set(jobs), "QUOTA_EXCEEDED", "", "jobs");
}

static void test_unknown_key(void) {
    reset();
    expectError(set(replace(MODBUS_JOB, R"("interval_s":60)", R"("intervall_s":60)")), "BAD_JOB", "mb_meters", "intervall_s");
}

static void test_one_invalid_job_rejects_set(void) {
    reset();
    std::string a = job("a1", "modbus_tcp", "192.168.1.60", 502, R"([{"id":"u1","args":{"address":"u1/hr0","count":2}}])");
    std::string b = replace(job("b2", "modbus_tcp", "192.168.1.61", 502, R"([{"id":"u1","args":{"address":"u1/hr0","count":2}}])"),
                            R"("interval_s":60)", R"("interval_s":30)");
    std::string c = job("c3", "modbus_tcp", "192.168.1.62", 502, R"([{"id":"u1","args":{"address":"u1/hr0","count":2}}])");
    expectError(set(a + "," + b + "," + c), "BAD_JOB", "b2", "interval_s");
}

static void test_shared_source_other_target(void) {
    reset();
    std::string a = replace(MODBUS_JOB, "src_9c1d", "src_7f3a");
    std::string b = replace(replace(MODBUS_JOB, "src_9c1d", "src_7f3a"), "mb_meters", "mb_other");
    b = replace(b, "192.168.1.60", "192.168.1.61");
    expectError(set(a + "," + b), "BAD_JOB", "mb_other", "source_id");
    // Same target and module: fine.
    std::string c = replace(replace(MODBUS_JOB, "src_9c1d", "src_7f3a"), "mb_meters", "mb_other");
    expectValid(set(a + "," + c));
}

static void test_run_larger_than_queue(void) {
    reset();
    std::string steps = R"([{"id":"u1","max_bytes":65536,"args":{"address":"u1/hr0","count":2}},)"
                        R"({"id":"u2","max_bytes":65536,"args":{"address":"u2/hr0","count":2}},)"
                        R"({"id":"u3","max_bytes":65536,"args":{"address":"u3/hr0","count":2}}])";
    expectError(set(job("big", "modbus_tcp", "192.168.1.60", 502, steps)), "QUOTA_EXCEEDED", "big", "max_bytes");
}

static void test_deadline_longer_than_half_interval(void) {
    reset();
    expectError(set(job("dl", "modbus_tcp", "192.168.1.60", 502, R"([{"id":"u1","args":{"address":"u1/hr0","count":2}}])",
                        R"(,"deadline_s":45)")),
                "BAD_JOB", "dl", "deadline_s");
    expectValid(set(job("dl", "modbus_tcp", "192.168.1.60", 502, R"([{"id":"u1","args":{"address":"u1/hr0","count":2}}])",
                        R"(,"deadline_s":30)")));
}

static void test_field_ranges(void) {
    reset();
    const char *mb = R"([{"id":"u1","args":{"address":"u1/hr0","count":2}}])";
    expectError(set(job("r", "modbus_tcp", "192.168.1.60", 502, mb, R"(,"jitter_s":31)")), "BAD_JOB", "r", "jitter_s");
    expectError(set(job("r", "modbus_tcp", "192.168.1.60", 502, mb, R"(,"step_timeout_ms":50)")), "BAD_JOB", "r", "step_timeout_ms");
    expectError(set(job("r", "modbus_tcp", "192.168.1.60", 502, mb, R"(,"max_bytes":0)")), "BAD_JOB", "r", "max_bytes");
    expectError(set(job("r", "modbus_tcp", "192.168.1.60", 502, mb, R"(,"publish":"later")")), "BAD_JOB", "r", "publish");
    expectError(set(job("r", "nope", "192.168.1.60", 502, mb)), "BAD_JOB", "r", "module");
    expectError(set(job("r", "modbus_tcp", "192.168.1.600", 502, mb)), "BAD_JOB", "r", "target");
    expectError(set(job("Bad", "modbus_tcp", "192.168.1.60", 502, mb)), "BAD_JOB", "", "id");
    expectError(set(job("r", "modbus_tcp", "192.168.1.60", 502, "[]")), "BAD_JOB", "r", "steps");
    expectError(set(job("r", "modbus_tcp", "192.168.1.60", 502, R"([{"id":"u1","args":{"address":"u1/hr0","count":200}}])")), "BAD_JOB",
                "r", "count");
    expectError(set(job("r", "modbus_tcp", "192.168.1.60", 502, R"([{"id":"u1","args":{"address":"u1/xx0","count":2}}])")), "BAD_JOB",
                "r", "address");
    expectError(set(replace(MODBUS_JOB, R"("rev":1)", R"("rev":0)")), "BAD_JOB", "mb_meters", "rev");
    expectError(set(replace(MODBUS_JOB, R"("enabled":true)", R"("enabled":1)")), "BAD_JOB", "mb_meters", "enabled");
    expectError(set(replace(MODBUS_JOB, "src_9c1d", "src_9C1D")), "BAD_JOB", "mb_meters", "source_id");
    expectError(set(replace(MODBUS_JOB, R"("id":"u4")", R"("id":"u3")")), "BAD_JOB", "mb_meters", "id");
    expectError(set(std::string(MODBUS_JOB) + "," + MODBUS_JOB), "BAD_JOB", "mb_meters", "id");
    expectError(set(replace(MODBUS_JOB, R"("interval_s":60)", R"("interval_s":60.0)")), "BAD_JOB", "mb_meters", "interval_s");
}

static void test_step_and_var_quotas(void) {
    reset();
    std::string nine = "[";
    for (int i = 0; i < 9; i++) {
        char step[96];
        snprintf(step, sizeof(step), R"(%s{"id":"u%d","max_bytes":100,"args":{"address":"u1/hr0","count":2}})", i ? "," : "", i);
        nine += step;
    }
    nine += "]";
    expectError(set(job("q", "modbus_tcp", "192.168.1.60", 502, nine)), "QUOTA_EXCEEDED", "q", "steps");

    const char *mb = R"([{"id":"u1","args":{"address":"u1/hr0","count":2}}])";
    expectError(set(job("q", "modbus_tcp", "192.168.1.60", 502, mb, R"(,"vars":{"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9})")),
                "QUOTA_EXCEEDED", "q", "vars");
    std::string longValue(2049, 'x');
    expectError(set(job("q", "modbus_tcp", "192.168.1.60", 502, mb, R"(,"vars":{"a":")" + longValue + "\"}")), "QUOTA_EXCEEDED", "q", "vars");
    expectError(set(job("q", "modbus_tcp", "192.168.1.60", 502, mb, R"(,"vars":{"a":true})")), "BAD_JOB", "q", "vars");
    // No source clock offset: the name is an ordinary var, the source clock is fixed at the source.
    expectValid(set(job("q", "modbus_tcp", "192.168.1.60", 502, mb, R"(,"vars":{"clock_offset_s":604801})")));
    expectError(set(job("q", "modbus_tcp", "192.168.1.60", 502, mb, R"(,"vars":{"overlap_s":600,"backfill_s":300})")), "BAD_JOB", "q",
                "backfill_s");
    expectError(set(job("q", "modbus_tcp", "192.168.1.60", 502, mb, R"(,"vars":{"window_max_s":30})")), "BAD_JOB", "q", "window_max_s");

    std::string login = R"("session":{"ttl_s":600,"login":[)";
    for (int i = 0; i < 5; i++) {
        char step[96];
        snprintf(step, sizeof(step), R"(%s{"id":"l%d","args":{"method":"GET","path":"/"}})", i ? "," : "", i);
        login += step;
    }
    login += "]}";
    expectError(set(job("q", "http", "192.168.1.50", 80, httpStep("GET", "/"), "," + login)), "QUOTA_EXCEEDED", "q", "login");

    std::string captures = R"([{"id":"s1","args":{"method":"GET","path":"/"},"captures":[)";
    for (int i = 0; i < 9; i++) {
        char capture[96];
        snprintf(capture, sizeof(capture), R"(%s{"name":"c%d","from":"body","key":"k%d"})", i ? "," : "", i, i);
        captures += capture;
    }
    captures += "]}]";
    expectError(set(job("q", "http", "192.168.1.50", 80, captures)), "QUOTA_EXCEEDED", "q", "captures");
}

static void test_capture_schema(void) {
    reset();
    auto withCapture = [](const char *capture) {
        return set(job("c", "http", "192.168.1.50", 80, std::string(R"([{"id":"s1","args":{"method":"GET","path":"/"},"captures":[)") + capture + "]}]"));
    };
    expectValid(withCapture(R"({"name":"tok","from":"body","key":"access_token"})"));
    expectValid(withCapture(R"({"name":"sid","from":"header","header":"set-cookie","after":"SID=","before":";"})"));
    expectValid(withCapture(R"({"name":"sid","from":"header","header":"set-cookie"})"));
    expectError(withCapture(R"({"name":"x","from":"body","after":"a="})"), "BAD_JOB", "c", "before");
    expectError(withCapture(R"({"name":"x","from":"body","key":"k","after":"a="})"), "BAD_JOB", "c", "after");
    expectError(withCapture(R"({"name":"x","from":"header"})"), "BAD_JOB", "c", "header");
    expectError(withCapture(R"({"name":"x","from":"cookie","key":"k"})"), "BAD_JOB", "c", "from");
    expectError(withCapture(R"({"name":"x","from":"body","key":"k","regex":"."})"), "BAD_JOB", "c", "regex");
    std::string longDelimiter(33, 'd');
    expectError(withCapture((R"({"name":"x","from":"body","after":")" + longDelimiter + R"(","before":"&"})").c_str()), "BAD_JOB", "c", "after");
}

// ---------------------------------------------------------------------------
// Read-only classing
// ---------------------------------------------------------------------------

// The XWEB POSTs are read by declaration: no grant list entry is needed.
static void test_xweb_needs_no_grant(void) {
    reset();
    expectValid(set(XWEB_JOB));
}

static void test_modbus_write_refused(void) {
    reset();
    expectError(set(job("w", "modbus_tcp", "192.168.1.60", 502, R"([{"id":"u1","args":{"address":"u1/hr0","values":[1,2]}}])")),
                "WRITE_NOT_ALLOWED", "w", "steps[0]");
    expectError(set(job("w", "modbus_tcp", "192.168.1.60", 502,
                        R"([{"id":"u1","args":{"address":"u1/hr0","count":2}},{"id":"u2","args":{"address":"u1/co0","values":[1],"write_multiple":true}}])")),
                "WRITE_NOT_ALLOWED", "w", "steps[1]");
}

static void test_get_with_body_refused(void) {
    reset();
    std::string body = R"(,"body":"{\"method\":\"Switch.Set\",\"params\":{\"id\":0,\"on\":false}}")";
    expectError(set(job("sh", "http", "192.168.1.70", 80, httpStep("GET", "/rpc", body))), "WRITE_NOT_ALLOWED", "sh", "steps[0]");
    expectError(set(job("sh", "http", "192.168.1.70", 80, httpStep("GET", "/rpc", R"(,"body_b64":"eA==")"))), "WRITE_NOT_ALLOWED", "sh",
                "steps[0]");
    // An empty body is no body.
    expectValid(set(job("sh", "http", "192.168.1.70", 80, httpStep("GET", "/rpc", R"(,"body":"")"))));
}

static void test_access_is_bad_job(void) {
    reset();
    expectError(set(job("a", "http", "192.168.1.50", 80, R"([{"id":"s1","access":"read","args":{"method":"POST","path":"/cgi-bin/login.lua"}}])")),
                "BAD_JOB", "a", "access");
    expectError(set(job("a", "http", "192.168.1.50", 80, httpStep("POST", "/cgi-bin/login.lua", R"(,"access":"read")"))), "BAD_JOB", "a",
                "access");
    expectError(set(job("a", "http", "192.168.1.50", 80, httpStep("GET", "/", R"(,"writes":"allow")"))), "BAD_JOB", "a", "writes");
    expectError(set(job("a", "http", "192.168.1.50", 80, httpStep("GET", "/", R"(,"timeout_ms":100)"))), "BAD_JOB", "a", "timeout_ms");
    expectError(set(job("a", "http", "192.168.1.50", 80, httpStep("GET", "/", R"(,"target":{})"))), "BAD_JOB", "a", "target");
}

static void test_non_get_steps_are_literal(void) {
    reset();
    expectValid(set(job("x", "http", "192.168.1.50", 80, httpStep("POST", "/cgi-bin/datachart.cgi?a=1"))));
    expectValid(set(job("x", "http", "192.168.1.50", 80, httpStep("DELETE", "/cgi-bin/datachart.cgi"))));
    expectValid(set(job("x", "http", "192.168.1.50", 80, httpStep("PUT", "/cgi-bin/login.lua", R"(,"body":"a=${now}")"))));
    // A templated non-GET path is not a literal read.
    expectError(set(job("x", "http", "192.168.1.50", 80, httpStep("POST", "/cgi-bin/datachart.cgi?t=${now}"))), "WRITE_NOT_ALLOWED", "x",
                "steps[0]");
}

static void test_state_changing_get_accepted(void) {
    reset();
    expectValid(set(job("sh", "http", "192.168.1.70", 80, httpStep("GET", "/rpc/Switch.Set?on=false"))));
    // A templated GET path is still read by method.
    expectValid(set(job("sh", "http", "192.168.1.70", 80, httpStep("GET", "/data?from=${now}"))));
}

static void test_udp_and_tcp_classing(void) {
    reset();
    // Literal payloads are read by declaration; a templated payload is refused.
    const char *frame = "AQIDBAUGBwgJCgsMDQ4PEBE="; // 17 bytes
    std::string udp = std::string(R"([{"id":"q","args":{"payload_b64":")") + frame + R"(","wait_ms":500}}])";
    expectValid(set(job("u", "udp", "192.168.1.80", 47808, udp)));
    std::string status = R"([{"id":"t","args":{"payload":"STATUS\r\n"}}])";
    expectValid(set(job("t", "tcp", "192.168.1.81", 4000, status)));
    expectError(set(job("t", "tcp", "192.168.1.81", 4000, R"([{"id":"t","args":{"payload":"T=${now}"}}])")), "WRITE_NOT_ALLOWED", "t",
                "steps[0]");
    expectValid(set(job("t", "tcp", "192.168.1.82", 4000, R"([{"id":"t","args":{"idle_ms":300}}])")));
    expectError(set(job("t", "udp", "192.168.1.82", 4000, R"([{"id":"t","args":{"wait_ms":300}}])")), "BAD_JOB", "t", "payload_b64");
    expectError(set(job("t", "udp", "192.168.1.82", 4000, R"([{"id":"t","args":{"payload_b64":"!!!!"}}])")), "BAD_JOB", "t", "payload_b64");
}

// ---------------------------------------------------------------------------
// Targets, templates, modules and stage order
// ---------------------------------------------------------------------------

static void test_target_mac(void) {
    reset();
    const char *target = R"("target":{"ip":"192.168.1.60","port":502})";
    expectValid(set(MODBUS_JOB));
    TEST_ASSERT_FALSE(table.jobs[0].hasMac);
    expectValid(set(replace(MODBUS_JOB, target, R"("target":{"ip":"192.168.1.60","port":502,"mac":"02:11:22:33:44:55"})")));
    TEST_ASSERT_TRUE(table.jobs[0].hasMac);
    TEST_ASSERT_EQUAL_HEX8(0x02, table.jobs[0].mac[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, table.jobs[0].mac[5]);
    const char *bad[] = {"02:11:22:33:44:5G", "02-11-22-33-44-55", "ff:ff:ff:ff:ff:ff", "00:00:00:00:00:00", ""};
    for (const char *mac : bad) {
        std::string doc = replace(MODBUS_JOB, target, std::string(R"("target":{"ip":"192.168.1.60","port":502,"mac":")") + mac + "\"}");
        expectError(set(doc), "BAD_JOB", "mb_meters", "target");
    }
    expectError(set(replace(MODBUS_JOB, target, R"("target":{"ip":"192.168.1.60","port":502,"mac":1})")), "BAD_JOB", "mb_meters", "target");
}

static void test_target_rules(void) {
    reset();
    targetVerdict = 1;
    expectError(set(MODBUS_JOB), "TARGET_NOT_PRIVATE", "mb_meters", "target");
    targetVerdict = 2;
    expectError(set(MODBUS_JOB), "TARGET_NOT_ALLOWED", "mb_meters", "target");
    // Classing comes first.
    expectError(set(job("w", "modbus_tcp", "192.168.1.60", 502, R"([{"id":"u1","args":{"address":"u1/hr0","values":[1]}}])")),
                "WRITE_NOT_ALLOWED", "w", "steps[0]");
}

static void test_template_check(void) {
    reset();
    // Capture used before it is set: step 1 uses sses, step 2 captures it.
    std::string steps = R"([{"id":"a","args":{"method":"GET","path":"/a?s=${cap:sses}"}},)"
                        R"({"id":"b","args":{"method":"GET","path":"/b"},"captures":[{"name":"sses","from":"body","key":"sses"}]}])";
    expectError(set(job("t", "http", "192.168.1.50", 80, steps)), "BAD_TEMPLATE", "t", "steps[0]");
    // A step cannot use its own capture; a later step can.
    steps = R"([{"id":"a","args":{"method":"GET","path":"/a?s=${cap:sses}"},"captures":[{"name":"sses","from":"body","key":"sses"}]}])";
    expectError(set(job("t", "http", "192.168.1.50", 80, steps)), "BAD_TEMPLATE", "t", "steps[0]");
    steps = R"([{"id":"a","args":{"method":"GET","path":"/a"},"captures":[{"name":"tok","from":"body","key":"next"}]},)"
            R"({"id":"b","args":{"method":"GET","path":"/b?t=${cap:tok|url}&r=${cap:tok}","headers":{"X-T":"${cap:tok|b64|url}"}}}])";
    expectValid(set(job("t", "http", "192.168.1.50", 80, steps)));
    TEST_ASSERT_EQUAL_UINT8(3, table.jobs[0].captures[0].chainCount);
    TEST_ASSERT_FALSE(table.jobs[0].usesTime);

    const char *bad[] = {"/x?${foo}",      "/x?${var:missing}", "/x?${now:s}",     "/x?${secret}",
                         "/x?${var:user",  "/x?${NOW}",         "/x?${now|gzip}",  "/x?${now|url|url|url|url}",
                         "/x?${cap:nope}", "/x?${}",            "/x?${cursor:sec}"};
    for (const char *path : bad) {
        std::string vars = R"(,"vars":{"user":"u"})";
        TEST_ASSERT_FALSE_MESSAGE(validate(set(job("t", "http", "192.168.1.50", 80, httpStep("GET", path), vars))), path);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("BAD_TEMPLATE", error.code, path);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("steps[0]", error.field, path);
    }
    const char *good[] = {"/x?${secret:xweb_pw}", "/x?a=$${literal}", "/x?a=$5", "/x?${now:ms}&${cursor:ms}", "/x?${var:user|json|url}"};
    for (const char *path : good) {
        std::string vars = R"(,"vars":{"user":"u"})";
        TEST_ASSERT_TRUE_MESSAGE(validate(set(job("t", "http", "192.168.1.50", 80, httpStep("GET", path), vars))), path);
    }
    TEST_ASSERT_TRUE(table.jobs[0].usesTime == false); // last template uses no time
    TEST_ASSERT_TRUE(validate(set(job("t", "http", "192.168.1.50", 80, httpStep("GET", "/x?${now}")))));
    TEST_ASSERT_TRUE(table.jobs[0].usesTime);
    TEST_ASSERT_FALSE(table.jobs[0].usesCursor);

    // Template errors in the body and the header values, and in login steps.
    expectError(set(job("t", "http", "192.168.1.50", 80, httpStep("GET", "/", R"(,"headers":{"X":"${bogus}"})"))), "BAD_TEMPLATE", "t",
                "steps[0]");
    std::string login = R"(,"session":{"ttl_s":600,"login":[{"id":"l","args":{"method":"GET","path":"/l?u=${var:nobody}"}}]})";
    expectError(set(job("t", "http", "192.168.1.50", 80, httpStep("GET", "/"), login)), "BAD_TEMPLATE", "t", "session.login[0]");
}

namespace {

GatewayLogic::ResolveStatus acceptAll(void *, const GatewayLogic::ResolveContext &, const char *, const char *, GatewayLogic::ResolveOutput *out) {
    out->length = 1;
    out->value[0] = 'v';
    return GatewayLogic::ResolveStatus::OK;
}

} // namespace

static void test_template_grammar_matches_engine(void) {
    // Syntax accepted by the static check == syntax accepted by expandTemplate.
    reset();
    const char *templates[] = {"/a${now}b",
                               "/a$${now}",
                               "/a$",
                               "/a${",
                               "/a${now",
                               "/a${now|url|json|b64}",
                               "/a${now|url|url|url|url}",
                               "/a${now:}",
                               "/a${var:user|xyz}",
                               "/a${var:user}x$y",
                               "/a${now:ms|b64}",
                               "/a${var:abcdefghijklmnopqrstuvwxyz0123456}",
                               "/a${secret:A.b-C_9}",
                               "/a${ now}"};
    GatewayLogic::Resolver chain{acceptAll, nullptr, nullptr};
    GatewayLogic::ResolveContext context{Module::HTTP, {192, 168, 1, 50}, 80, true};
    for (const char *t : templates) {
        uint8_t out[256], scratch[256];
        GatewayLogic::ExpandResult result{};
        bool engine = GatewayLogic::expandTemplate(t, strlen(t), &chain, 1, context, out, sizeof(out), scratch, sizeof(scratch), &result) ==
                      GatewayLogic::ExpandStatus::OK;
        std::string vars = R"(,"vars":{"user":"u","abcdefghijklmnopqrstuvwxyz012345":"x"})";
        bool collector = validate(set(job("t", "http", "192.168.1.50", 80, httpStep("GET", t), vars)));
        TEST_ASSERT_EQUAL_MESSAGE(engine, collector, t);
    }
}

static void test_module_unavailable_and_order(void) {
    reset();
    std::string https = job("s", "https", "192.168.1.90", 443,
                            R"([{"id":"a","args":{"method":"GET","path":"/","cert_sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"}}])");
    expectValid(set(https));
    httpsAvailable = false;
    expectError(set(https), "MODULE_UNAVAILABLE", "s", "module");
    // BAD_TEMPLATE before MODULE_UNAVAILABLE, across jobs.
    std::string badTemplate = job("bt", "http", "192.168.1.50", 80, httpStep("GET", "/${bogus}"));
    expectError(set(https + "," + badTemplate), "BAD_TEMPLATE", "bt", "steps[0]");
    // BAD_JOB in a later job before BAD_TEMPLATE in an earlier one.
    std::string badJob = replace(job("bj", "http", "192.168.1.51", 80, httpStep("GET", "/")), R"("interval_s":60)", R"("interval_s":59)");
    expectError(set(badTemplate + "," + badJob), "BAD_JOB", "bj", "interval_s");
    // https needs cert_sha256.
    httpsAvailable = true;
    expectError(set(job("s", "https", "192.168.1.90", 443, httpStep("GET", "/"))), "BAD_JOB", "s", "cert_sha256");
}

static void test_http_args_schema(void) {
    reset();
    expectError(set(job("h", "http", "192.168.1.50", 80, httpStep("GET", "/", R"(,"headers":{"Host":"x"})"))), "BAD_JOB", "h", "headers");
    expectError(set(job("h", "http", "192.168.1.50", 80, httpStep("GET", "/", R"(,"headers":{"X":1})"))), "BAD_JOB", "h", "headers");
    expectError(set(job("h", "http", "192.168.1.50", 80, httpStep("BREW", "/"))), "BAD_JOB", "h", "method");
    expectError(set(job("h", "http", "192.168.1.50", 80, httpStep("GET", "no-slash"))), "BAD_JOB", "h", "path");
    expectError(set(job("h", "http", "192.168.1.50", 80, httpStep("GET", "/", R"(,"body":"a","body_b64":"YQ==")"))), "BAD_JOB", "h", "body_b64");
    expectError(set(job("h", "http", "192.168.1.50", 80, httpStep("GET", "/", R"(,"body_b64":"YQ=")"))), "BAD_JOB", "h", "body_b64");
    expectValid(set(job("h", "http", "192.168.1.50", 80, R"([{"id":"s1","args":{}}])")));
    TEST_ASSERT_EQUAL_STRING("GET", table.jobs[0].steps[0].args.method);
    TEST_ASSERT_EQUAL_STRING("/", table.jobs[0].steps[0].args.path);
    expectValid(set(job("h", "http", "192.168.1.50", 80, httpStep("POST", "/cgi-bin/login.lua", R"(,"body_b64":"AAEC/w==")"))));
    const StepArgs &args = table.jobs[0].steps[0].args;
    TEST_ASSERT_EQUAL_UINT16(4, args.bodyBytesLength);
    TEST_ASSERT_EQUAL_HEX8(0xFF, args.bodyBytes[3]);
}

// ---------------------------------------------------------------------------
// Binding hash
// ---------------------------------------------------------------------------

static void test_binding_hash(void) {
    reset();
    expectValid(set(XWEB_JOB));
    uint8_t base[32];
    memcpy(base, table.jobs[0].bindingHash, 32);
    char canonical[2048];
    TEST_ASSERT_TRUE(canonicalBinding(table.jobs[0], canonical, sizeof(canonical)) > 0);
    TEST_ASSERT_EQUAL_size_t(0, canonicalBinding(table.jobs[0], canonical, 16)); // does not fit

    // Reordered keys and a changed interval or data step: same binding.
    std::string reordered = replace(XWEB_JOB, R"("target":{"ip":"192.168.1.50","port":80})", R"("target":{"port":80,"ip":"192.168.1.50"})");
    reordered = replace(reordered, R"("ttl_s":1800,"expiry":{"status":[401,403],"marker":"login.lua"},)", "");
    reordered = replace(reordered, R"("session":{)", R"("session":{"expiry":{"marker":"login.lua","status":[401,403]},"ttl_s":1800,)");
    reordered = replace(reordered, R"("interval_s":300)", R"("interval_s":600)");
    expectValid(set(reordered));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(base, table.jobs[0].bindingHash, 32);

    // Target, session or module change: different binding.
    expectValid(set(replace(XWEB_JOB, "192.168.1.50", "192.168.1.51")));
    TEST_ASSERT_TRUE(memcmp(base, table.jobs[0].bindingHash, 32) != 0);
    expectValid(set(replace(XWEB_JOB, R"("ttl_s":1800)", R"("ttl_s":1700)")));
    TEST_ASSERT_TRUE(memcmp(base, table.jobs[0].bindingHash, 32) != 0);
    expectValid(set(replace(XWEB_JOB, R"("after":"sses=")", R"("after":"sid=")")));
    TEST_ASSERT_TRUE(memcmp(base, table.jobs[0].bindingHash, 32) != 0);
    // Job-level limits the login step inherits are not session values.
    expectValid(set(replace(replace(XWEB_JOB, R"("max_bytes":131072)", R"("max_bytes":65536)"), R"("step_timeout_ms":8000)",
                            R"("step_timeout_ms":5000)")));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(base, table.jobs[0].bindingHash, 32);
}

// ---------------------------------------------------------------------------
// Captures
// ---------------------------------------------------------------------------

namespace {

Capture bodyKey(const char *key) {
    Capture c{};
    snprintf(c.name, sizeof(c.name), "c");
    c.key = key;
    return c;
}

Capture headerCapture(const char *header, const char *after, const char *before) {
    Capture c{};
    snprintf(c.name, sizeof(c.name), "c");
    c.fromHeader = true;
    c.header = header;
    c.after = after;
    c.before = before;
    return c;
}

uint8_t value[4096];
size_t valueLength;

CaptureStatus fromBody(const Capture &c, const std::string &body) {
    return extractCapture(c, nullptr, 0, reinterpret_cast<const uint8_t *>(body.data()), body.size(), value, sizeof(value), &valueLength);
}

CaptureStatus fromHeaders(const Capture &c, const char *headers) {
    return extractCapture(c, headers, strlen(headers), nullptr, 0, value, sizeof(value), &valueLength);
}

std::string captured() { return std::string(reinterpret_cast<const char *>(value), valueLength); }

} // namespace

static void test_capture_json_key(void) {
    TEST_ASSERT_TRUE(fromBody(bodyKey("sses"), R"({"sses":"4f2a9c","user":"op"})") == CaptureStatus::OK);
    TEST_ASSERT_EQUAL_STRING("4f2a9c", captured().c_str());
    TEST_ASSERT_TRUE(fromBody(bodyKey("n"), R"( { "a" : [1,{"n":2}], "n" : 12.50 } )") == CaptureStatus::OK);
    TEST_ASSERT_EQUAL_STRING("12.50", captured().c_str());
    TEST_ASSERT_TRUE(fromBody(bodyKey("sses"), R"({"o":{"sses":"inner"},"s":"}\"{","sses":"top"})") == CaptureStatus::OK);
    TEST_ASSERT_EQUAL_STRING("top", captured().c_str());
    TEST_ASSERT_TRUE(fromBody(bodyKey("t"), R"({"t":"a\"b\\c\/dé😀"})") == CaptureStatus::OK);
    TEST_ASSERT_EQUAL_STRING("a\"b\\c/d\xC3\xA9\xF0\x9F\x98\x80", captured().c_str());
    TEST_ASSERT_TRUE(fromBody(bodyKey("t"), R"({"t":true})") == CaptureStatus::MISSING);
    TEST_ASSERT_TRUE(fromBody(bodyKey("t"), R"({"t":""})") == CaptureStatus::MISSING);
    TEST_ASSERT_TRUE(fromBody(bodyKey("t"), R"({"u":"x"})") == CaptureStatus::MISSING);
    TEST_ASSERT_TRUE(fromBody(bodyKey("t"), R"(["t","x"])") == CaptureStatus::MISSING);
    TEST_ASSERT_TRUE(fromBody(bodyKey("t"), R"({"a":"x" "t":"y"})") == CaptureStatus::MISSING);
}

static void test_capture_header_cookie(void) {
    const char *reply = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nSet-Cookie: SID=ab12; Path=/\r\n\r\n";
    TEST_ASSERT_TRUE(fromHeaders(headerCapture("set-cookie", "SID=", ";"), reply) == CaptureStatus::OK);
    TEST_ASSERT_EQUAL_STRING("ab12", captured().c_str());
}

static void test_capture_second_set_cookie_line(void) {
    const char *reply = "HTTP/1.1 302 Found\r\nSet-Cookie: csrftoken=x9; Path=/\r\nset-cookie: sessionid=q7w; HttpOnly\r\n\r\n";
    TEST_ASSERT_TRUE(fromHeaders(headerCapture("set-cookie", "sessionid=", ";"), reply) == CaptureStatus::OK);
    TEST_ASSERT_EQUAL_STRING("q7w", captured().c_str());
    // Without `after`: the first line, cut at `before`.
    TEST_ASSERT_TRUE(fromHeaders(headerCapture("Set-Cookie", nullptr, ";"), reply) == CaptureStatus::OK);
    TEST_ASSERT_EQUAL_STRING("csrftoken=x9", captured().c_str());
    // `before` absent from the line: the rest of the value.
    TEST_ASSERT_TRUE(fromHeaders(headerCapture("set-cookie", "sessionid=", "#"), reply) == CaptureStatus::OK);
    TEST_ASSERT_EQUAL_STRING("q7w; HttpOnly", captured().c_str());
    // Header block ends at the blank line; a status line is never a header.
    TEST_ASSERT_TRUE(fromHeaders(headerCapture("x-tok", nullptr, nullptr), "X-Tok: a\r\n\r\nX-Tok: b\r\n") == CaptureStatus::MISSING);
    TEST_ASSERT_TRUE(fromHeaders(headerCapture("x-tok", nullptr, nullptr), "HTTP/1.1 200 OK\r\n\r\nX-Tok: b\r\n") == CaptureStatus::MISSING);
    TEST_ASSERT_TRUE(fromHeaders(headerCapture("set-cookie", "nothere=", ";"), reply) == CaptureStatus::MISSING);
}

static void test_capture_jwt_and_expansion(void) {
    std::string jwt(1200, 'J');
    TEST_ASSERT_TRUE(fromBody(bodyKey("access_token"), R"({"access_token":")" + jwt + "\"}") == CaptureStatus::OK);
    TEST_ASSERT_EQUAL_size_t(1200, valueLength);
    // `Authorization: Bearer ${cap:tok}` expands within the 5760-byte request limit.
    struct Ctx {
        const uint8_t *v;
        size_t n;
    } ctx{value, valueLength};
    GatewayLogic::Resolver resolver{[](void *c, const GatewayLogic::ResolveContext &, const char *name, const char *, GatewayLogic::ResolveOutput *out) {
                                        if (strcmp(name, "cap") != 0) return GatewayLogic::ResolveStatus::UNKNOWN;
                                        Ctx *x = static_cast<Ctx *>(c);
                                        memcpy(out->value, x->v, x->n);
                                        out->length = x->n;
                                        out->sensitive = true;
                                        return GatewayLogic::ResolveStatus::OK;
                                    },
                                    &ctx, nullptr};
    const char *tmpl = "Bearer ${cap:tok}";
    static uint8_t out[5760], scratch[2 * CAPTURE_VALUE_MAX];
    GatewayLogic::ExpandResult result{};
    GatewayLogic::ResolveContext rc{Module::HTTP, {192, 168, 1, 50}, 80, true};
    TEST_ASSERT_TRUE(GatewayLogic::expandTemplate(tmpl, strlen(tmpl), &resolver, 1, rc, out, sizeof(out), scratch, sizeof(scratch), &result) ==
                     GatewayLogic::ExpandStatus::OK);
    TEST_ASSERT_EQUAL_size_t(1207, result.length);
    TEST_ASSERT_TRUE(result.sensitive);
}

static void test_capture_over_limit_missing(void) {
    std::string big(2100, 'v');
    memset(value, 0xAA, sizeof(value));
    TEST_ASSERT_TRUE(fromBody(bodyKey("t"), R"({"t":")" + big + "\"}") == CaptureStatus::MISSING);
    TEST_ASSERT_EQUAL_size_t(0, valueLength);
    for (size_t i = 0; i < CAPTURE_VALUE_MAX; i++) TEST_ASSERT_EQUAL_HEX8(0, value[i]); // no partial value kept
    std::string exact(2048, 'v');
    TEST_ASSERT_TRUE(fromBody(bodyKey("t"), R"({"t":")" + exact + "\"}") == CaptureStatus::OK);
    Capture delimited{};
    delimited.after = "sses=";
    delimited.before = "&";
    TEST_ASSERT_TRUE(fromBody(delimited, "a=1&sses=" + big + "&b") == CaptureStatus::MISSING);
}

static void test_capture_delimiters(void) {
    Capture c{};
    c.after = "sses=";
    c.before = "&";
    TEST_ASSERT_TRUE(fromBody(c, "ok=1&sses=0123456789abcdef&user=op") == CaptureStatus::OK);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef", captured().c_str());
    TEST_ASSERT_TRUE(fromBody(c, "<html>login.lua</html>") == CaptureStatus::MISSING); // marker not found
    TEST_ASSERT_TRUE(fromBody(c, "sses=abc") == CaptureStatus::MISSING);              // no closing delimiter
    TEST_ASSERT_TRUE(fromBody(c, "sses=&x") == CaptureStatus::MISSING);               // empty value
}

static void test_expiry_rule(void) {
    reset();
    expectValid(set(XWEB_JOB));
    const Job &j = table.jobs[0];
    const char *page = "<html><form action=\"/cgi-bin/login.lua\"></html>";
    TEST_ASSERT_TRUE(isExpired(j, 200, reinterpret_cast<const uint8_t *>(page), strlen(page)));
    TEST_ASSERT_TRUE(isExpired(j, 401, nullptr, 0));
    TEST_ASSERT_TRUE(isExpired(j, 403, nullptr, 0));
    TEST_ASSERT_FALSE(isExpired(j, 200, reinterpret_cast<const uint8_t *>("{\"13\":[]}"), 9));
    TEST_ASSERT_FALSE(isExpired(j, 404, nullptr, 0));
    expectValid(set(MODBUS_JOB));
    TEST_ASSERT_FALSE(isExpired(table.jobs[0], 401, nullptr, 0)); // no session
}

static void test_apply_chain(void) {
    const uint8_t raw[] = "a b&c";
    uint8_t out[64], scratch[64];
    EncodingChain none{0, {}};
    TEST_ASSERT_EQUAL_size_t(5, applyChain(none, raw, 5, out, sizeof(out), scratch, sizeof(scratch)));
    EncodingChain url{1, {Encoding::URL}};
    size_t n = applyChain(url, raw, 5, out, sizeof(out), scratch, sizeof(scratch));
    TEST_ASSERT_EQUAL_STRING_LEN("a%20b%26c", out, n);
    EncodingChain b64url{2, {Encoding::B64, Encoding::URL}};
    const uint8_t q[] = {0xFB, 0xFF};
    n = applyChain(b64url, q, 2, out, sizeof(out), scratch, sizeof(scratch));
    TEST_ASSERT_EQUAL_STRING_LEN("%2B%2F8%3D", out, n);
    EncodingChain three{3, {Encoding::JSON, Encoding::URL, Encoding::B64}};
    n = applyChain(three, reinterpret_cast<const uint8_t *>("\""), 1, out, sizeof(out), scratch, sizeof(scratch));
    TEST_ASSERT_EQUAL_STRING_LEN("JTVDJTIy", out, n); // \" -> %5C%22 -> base64
    TEST_ASSERT_EQUAL_size_t(0, applyChain(url, raw, 5, out, 4, scratch, sizeof(scratch)));
}

static void test_scrub_captures(void) {
    // Data step sent SSES=${cap:sses|url}; the reply echoes the raw and URL forms.
    const char *raw = "ab cd/ef";
    uint8_t encoded[32];
    bool ok = false;
    size_t encodedLength = GatewayLogic::encodeUrl(reinterpret_cast<const uint8_t *>(raw), strlen(raw), encoded, sizeof(encoded), &ok);
    SecretsLogic::ScrubForm forms[] = {{reinterpret_cast<const uint8_t *>(raw), strlen(raw), "sses"}, {encoded, encodedLength, "sses"}};
    char body[128];
    int n = snprintf(body, sizeof(body), "x=ab cd/ef;y=%.*s;z", static_cast<int>(encodedLength), encoded);
    uint8_t buffer[256], scratch[256];
    memcpy(buffer, body, static_cast<size_t>(n));
    bool truncated = false, redacted = false;
    size_t length = scrubCaptures(buffer, static_cast<size_t>(n), sizeof(buffer), scratch, forms, 2, &truncated, &redacted);
    TEST_ASSERT_EQUAL_STRING_LEN("x=****;y=****;z", buffer, length);
    TEST_ASSERT_TRUE(redacted);
    TEST_ASSERT_FALSE(truncated);

    // Nothing to replace: rd stays false.
    redacted = false;
    memcpy(buffer, "nothing here", 12);
    TEST_ASSERT_EQUAL_size_t(12, scrubCaptures(buffer, 12, sizeof(buffer), scratch, forms, 2, &truncated, &redacted));
    TEST_ASSERT_FALSE(redacted);
}

void run_collector_runtime_tests(void);

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_xweb_example);
    RUN_TEST(test_modbus_example);
    RUN_TEST(test_both_examples_in_one_set);
    RUN_TEST(test_defaults_applied);
    RUN_TEST(test_empty_set_and_missing_v);
    RUN_TEST(test_bad_json);
    RUN_TEST(test_ninth_job_quota);
    RUN_TEST(test_unknown_key);
    RUN_TEST(test_one_invalid_job_rejects_set);
    RUN_TEST(test_shared_source_other_target);
    RUN_TEST(test_run_larger_than_queue);
    RUN_TEST(test_deadline_longer_than_half_interval);
    RUN_TEST(test_field_ranges);
    RUN_TEST(test_step_and_var_quotas);
    RUN_TEST(test_capture_schema);
    RUN_TEST(test_xweb_needs_no_grant);
    RUN_TEST(test_modbus_write_refused);
    RUN_TEST(test_get_with_body_refused);
    RUN_TEST(test_access_is_bad_job);
    RUN_TEST(test_non_get_steps_are_literal);
    RUN_TEST(test_state_changing_get_accepted);
    RUN_TEST(test_udp_and_tcp_classing);
    RUN_TEST(test_target_mac);
    RUN_TEST(test_target_rules);
    RUN_TEST(test_template_check);
    RUN_TEST(test_template_grammar_matches_engine);
    RUN_TEST(test_module_unavailable_and_order);
    RUN_TEST(test_http_args_schema);
    RUN_TEST(test_binding_hash);
    RUN_TEST(test_capture_json_key);
    RUN_TEST(test_capture_header_cookie);
    RUN_TEST(test_capture_second_set_cookie_line);
    RUN_TEST(test_capture_jwt_and_expansion);
    RUN_TEST(test_capture_over_limit_missing);
    RUN_TEST(test_capture_delimiters);
    RUN_TEST(test_expiry_rule);
    RUN_TEST(test_apply_chain);
    RUN_TEST(test_scrub_captures);
    run_collector_runtime_tests();
    return UNITY_END();
}
