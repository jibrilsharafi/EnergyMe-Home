// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

// Unit tests for collector_logic run-time pieces: scheduling, backoff, window,
// step outcomes, part header (cloud golden parts), chunking, Modbus packing,
// out-queue, token bucket and health.

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "collector_logic.h"

using namespace CollectorLogic;
using GatewayLogic::Module;

// ---------------------------------------------------------------------------
// Scheduling and backoff
// ---------------------------------------------------------------------------

static void test_backoff_growth_and_cap(void) {
    TEST_ASSERT_EQUAL_UINT64(600, backoffDelayS(300, 1));
    TEST_ASSERT_EQUAL_UINT64(1200, backoffDelayS(300, 2));
    TEST_ASSERT_EQUAL_UINT64(2400, backoffDelayS(300, 3));
    TEST_ASSERT_EQUAL_UINT64(3600, backoffDelayS(300, 4));
    TEST_ASSERT_EQUAL_UINT64(3600, backoffDelayS(300, 40));
    TEST_ASSERT_EQUAL_UINT64(7200, backoffDelayS(7200, 3)); // cap is max(interval, 3600)
    TEST_ASSERT_EQUAL_UINT64(120, backoffDelayS(60, 1));
}

static void test_first_slot_spread(void) {
    TEST_ASSERT_EQUAL_UINT64(1000, firstSlotS(1000, 300, 0));
    TEST_ASSERT_EQUAL_UINT64(1042, firstSlotS(1000, 300, 42));
    TEST_ASSERT_EQUAL_UINT64(1060, firstSlotS(1000, 300, 500)); // at most 60 s
    TEST_ASSERT_EQUAL_UINT64(1060, firstSlotS(1000, 60, 61));   // at most the interval
}

static void test_no_burst_after_pause(void) {
    // A 60 s job paused 10 minutes runs once at its next slot.
    TEST_ASSERT_EQUAL_UINT64(1660, nextSlotS(1000, 60, 1600));
    TEST_ASSERT_EQUAL_UINT64(1660, nextSlotS(1000, 60, 1659));
    TEST_ASSERT_EQUAL_UINT64(1060, nextSlotS(1000, 60, 1000)); // strictly after now
    TEST_ASSERT_EQUAL_UINT64(1000, nextSlotS(1000, 60, 900));
    // Recovery after backoff: back on the regular grid.
    TEST_ASSERT_EQUAL_UINT64(1000 + 300 * 9, nextSlotS(1000, 300, 1000 + 300 * 8 + 7));
}

// ---------------------------------------------------------------------------
// Window and cursor
// ---------------------------------------------------------------------------

static void test_window_normal_advance_with_overlap(void) {
    Window w = computeWindow(1730380300, 1730380000, true, 600, 3600, 3600);
    TEST_ASSERT_EQUAL_UINT64(1730379400, w.from);
    TEST_ASSERT_EQUAL_UINT64(1730380300, w.to);
}

static void test_window_outage_longer_than_backfill(void) {
    uint64_t now = 1730400000;
    uint64_t cursor = now - 10 * 3600;
    Window w = computeWindow(now, cursor, true, 900, 21600, 3600);
    TEST_ASSERT_EQUAL_UINT64(now - 21600, w.from);
    TEST_ASSERT_EQUAL_UINT64(now - 21600 + 3600, w.to);
    // The next runs walk one hour each until the present.
    uint64_t later = now + 300;
    Window next = computeWindow(later, w.to, true, 900, 21600, 3600);
    TEST_ASSERT_EQUAL_UINT64(w.to - 900, next.from);
    TEST_ASSERT_EQUAL_UINT64(w.to - 900 + 3600, next.to);
}

static void test_window_truncated_halves_from_same_start(void) {
    uint64_t now = 1730400000, cursor = now - 7200;
    Window first = computeWindow(now, cursor, true, 0, 21600, 3600);
    uint32_t w = halveWindow(3600);
    TEST_ASSERT_EQUAL_UINT32(1800, w);
    Window retry = computeWindow(now + 300, cursor, true, 0, 21600, w); // cursor did not advance
    TEST_ASSERT_EQUAL_UINT64(first.from, retry.from);
    TEST_ASSERT_EQUAL_UINT64(first.from + 1800, retry.to);
}

static void test_window_halving_and_doubling_bounds(void) {
    TEST_ASSERT_EQUAL_UINT32(60, halveWindow(100));
    TEST_ASSERT_EQUAL_UINT32(60, halveWindow(60));
    TEST_ASSERT_EQUAL_UINT32(120, doubleWindow(60, 3600));
    TEST_ASSERT_EQUAL_UINT32(3600, doubleWindow(2400, 3600));
    TEST_ASSERT_EQUAL_UINT32(3600, doubleWindow(3600, 3600));
    uint32_t w = 3600;
    for (int i = 0; i < 10; i++) w = halveWindow(w);
    TEST_ASSERT_EQUAL_UINT32(60, w);
    for (int i = 0; i < 10; i++) w = doubleWindow(w, 3600);
    TEST_ASSERT_EQUAL_UINT32(3600, w);
}

static void test_window_clock_steps_back(void) {
    uint64_t now = 1730380000;
    Window w = computeWindow(now, now + 5000, true, 600, 3600, 3600); // cursor later than now
    TEST_ASSERT_EQUAL_UINT64(now - 3600, w.from);
    TEST_ASSERT_TRUE(w.to >= w.from);
    Window unset = computeWindow(now, 0, false, 600, 3600, 3600);
    TEST_ASSERT_EQUAL_UINT64(now - 3600, unset.from);
    Window early = computeWindow(100, 0, false, 600, 3600, 3600); // no underflow
    TEST_ASSERT_EQUAL_UINT64(0, early.from);
    TEST_ASSERT_EQUAL_UINT64(100, early.to);
}

static void test_source_clock(void) {
    TEST_ASSERT_EQUAL_INT64(1730374960, toSourceClock(1730380000, 5040));
    TEST_ASSERT_EQUAL_INT64(1730380100, toSourceClock(1730380000, -100));
}

// ---------------------------------------------------------------------------
// Step outcomes
// ---------------------------------------------------------------------------

static void test_step_fates(void) {
    TEST_ASSERT_TRUE(stepFate(Module::HTTP, true, nullptr, true) == StepFate::PUBLISH_CONTINUE);
    TEST_ASSERT_TRUE(stepFate(Module::MODBUS_TCP, false, "TIMEOUT", true) == StepFate::PART_CONTINUE);
    TEST_ASSERT_TRUE(stepFate(Module::MODBUS_TCP, false, "TIMEOUT", false) == StepFate::STOP);
    TEST_ASSERT_TRUE(stepFate(Module::MODBUS_TCP, false, "NO_REPLY", true) == StepFate::PART_CONTINUE);
    TEST_ASSERT_TRUE(stepFate(Module::MODBUS_TCP, false, "BAD_RESPONSE", false) == StepFate::STOP);
    TEST_ASSERT_TRUE(stepFate(Module::MODBUS_TCP, false, "MODBUS_EXCEPTION", true) == StepFate::PART_CONTINUE);
    TEST_ASSERT_TRUE(stepFate(Module::MODBUS_TCP, false, "CONNECT_FAILED", false) == StepFate::STOP);
    TEST_ASSERT_TRUE(stepFate(Module::HTTP, false, "CONNECT_FAILED", false) == StepFate::STOP);
    TEST_ASSERT_TRUE(stepFate(Module::HTTP, false, "TIMEOUT", true) == StepFate::STOP);
    TEST_ASSERT_TRUE(stepFate(Module::HTTPS, false, "NO_REPLY", true) == StepFate::STOP);
    TEST_ASSERT_TRUE(stepFate(Module::TCP, false, "NO_REPLY", true) == StepFate::STOP);
    TEST_ASSERT_TRUE(stepFate(Module::HTTP, false, "BAD_RESPONSE", true) == StepFate::PART_CONTINUE);
    TEST_ASSERT_TRUE(stepFate(Module::HTTP, false, "WRITE_NOT_ALLOWED", false) == StepFate::STOP);
    TEST_ASSERT_TRUE(stepFate(Module::HTTP, false, "SECRET_TARGET_MISMATCH", false) == StepFate::STOP);
    TEST_ASSERT_TRUE(stepFate(Module::HTTP, false, "BAD_ARGS", false) == StepFate::STOP);
    TEST_ASSERT_TRUE(stepFate(Module::UDP, false, "NO_REPLY", true) == StepFate::PART_CONTINUE);
    TEST_ASSERT_TRUE(stepFate(Module::UDP, false, "NETWORK_DOWN", false) == StepFate::STOP);
}

// ---------------------------------------------------------------------------
// Part header: cloud golden parts
// ---------------------------------------------------------------------------

namespace {

const char *const BOOT = "a1b2c3d4e5f60718";
const char *const XWEB_SRC = "src_7f3a91c2";
const char *const MB_SRC = "src_9c1d0b44";
const uint64_t TS_MS = 1793402820000ULL;

PartHeader xweb() {
    PartHeader h{};
    h.job = "xweb_hist";
    h.rev = 1;
    h.set = 8;
    h.src = XWEB_SRC;
    h.boot = BOOT;
    h.seq = 1;
    h.step = "chart";
    h.module = Module::HTTP;
    h.i = 1;
    h.n = 1;
    h.st = "OK";
    h.tsKnown = true;
    h.tsMs = TS_MS;
    h.upMs = 41200;
    return h;
}

PartHeader xwebData(uint32_t len, const char *sha) {
    PartHeader h = xweb();
    h.hasPs = true;
    h.ps = 200;
    h.hasCo = true;
    h.co = 5040;
    h.ct = "application/json";
    h.hasWin = true;
    h.winFrom = 1793396880;
    h.winTo = 1793397780;
    h.len = len;
    h.total = len;
    h.sha = sha;
    return h;
}

PartHeader modbus(const char *step, const char *addr, uint32_t cnt) {
    PartHeader h = xweb();
    h.job = "mb_meters";
    h.set = 3;
    h.src = MB_SRC;
    h.module = Module::MODBUS_TCP;
    h.step = step;
    h.addr = addr;
    h.cnt = cnt;
    return h;
}

PartHeader body(PartHeader h, uint32_t len, const char *sha) {
    h.len = len;
    h.total = len;
    h.sha = sha;
    return h;
}

PartHeader failed(PartHeader h, const char *st, const char *rc) {
    h.st = st;
    h.rc = rc;
    return h;
}

PartHeader withPs(PartHeader h, int ps) {
    h.hasPs = true;
    h.ps = ps;
    return h;
}

PartHeader withCo(PartHeader h) {
    h.hasCo = true;
    h.co = 5040;
    return h;
}

PartHeader login(PartHeader h) {
    h.login = true;
    h.step = "login";
    return h;
}

const char *const SHA_23 = "c66b3898918243d4edef7f6d45f54a8cc3662c7742cf00ede8eec1f0008bb693";
const char *const SHA_13_17 = "4a005d592e0492f004a0150c0a35e115e15c8caf88c6831506c22820a949c3f2";
const char *const SHA_1_7 = "12d74052d8039c6598935d83e90bec9352704b827de438277a6330e66cc854ac";
const char *const SHA_REGS = "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a";

struct Golden {
    const char *name;
    const char *line;
    PartHeader header;
    bool exact; // field order of the golden equals the encoder's order
};

PartHeader chunk(uint32_t i, uint32_t off, uint32_t len) {
    PartHeader h = withCo(withPs(xweb(), 200));
    h.seq = 2;
    h.i = i;
    h.n = 3;
    h.off = off;
    h.len = len;
    h.total = 958;
    h.sha = SHA_1_7;
    return h;
}

PartHeader binaryBody() {
    PartHeader h = body(xweb(), 900, "7ef083548a60be2cb2b46456df4f87d57e3aefac7290bb5d5bd5bf9367c05a94");
    h.job = "raw_tcp";
    h.step = "read";
    h.module = Module::TCP;
    return h;
}

PartHeader http404() {
    PartHeader h = body(withCo(withPs(xweb(), 404)), 120, "472ba4fc5b7cf83e9227a27cc6b3aef7c61fa5a7096a5bf8c69bb5fcaf5f981b");
    h.ct = "text/html";
    return h;
}

PartHeader loginOk() {
    PartHeader h = withCo(withPs(login(xweb()), 200));
    h.total = 300;
    return h;
}

PartHeader loginRejected() {
    PartHeader h = failed(login(xweb()), "REJECTED", "WRITE_NOT_ALLOWED");
    h.rdt = "WRITES_DENIED";
    return h;
}

PartHeader tsNull() {
    PartHeader h = body(modbus("u3", "u3/hr3000", 2), 4, SHA_REGS);
    h.tsKnown = false;
    return h;
}

PartHeader dateCarried() {
    PartHeader h = xwebData(913, SHA_13_17);
    h.date = "Fri, 30 Oct 2026 22:03:00 GMT";
    return h;
}

PartHeader truncatedData(uint32_t len, const char *sha, const char *ct) {
    PartHeader h = xwebData(len, sha);
    h.tr = true;
    h.ct = ct;
    return h;
}

// Header lines of the golden parts in energyme-infra
// projects/energyme-home-iot-pipeline/tests/unit/fixtures/collector/golden (copied to ./golden).
// login_leaked_body.part is a negative fixture the device never produces.
const Golden GOLDENS[] = {
    {"binary_body",
     R"({"v":1,"job":"raw_tcp","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"read","ph":"step","mod":"tcp","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"len":900,"total":900,"sha":"7ef083548a60be2cb2b46456df4f87d57e3aefac7290bb5d5bd5bf9367c05a94"})",
     binaryBody(), true},
    {"chunked_1_of_3",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":2,"step":"chart","ph":"step","mod":"http","i":1,"n":3,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"len":400,"total":958,"sha":"12d74052d8039c6598935d83e90bec9352704b827de438277a6330e66cc854ac","ps":200,"co":5040})",
     chunk(1, 0, 400), false},
    {"chunked_2_of_3",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":2,"step":"chart","ph":"step","mod":"http","i":2,"n":3,"off":400,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"len":400,"total":958,"sha":"12d74052d8039c6598935d83e90bec9352704b827de438277a6330e66cc854ac","ps":200,"co":5040})",
     chunk(2, 400, 400), false},
    {"chunked_3_of_3",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":2,"step":"chart","ph":"step","mod":"http","i":3,"n":3,"off":800,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"len":158,"total":958,"sha":"12d74052d8039c6598935d83e90bec9352704b827de438277a6330e66cc854ac","ps":200,"co":5040})",
     chunk(3, 800, 158), false},
    {"header_only_failure",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"FAILED","ts":1793402820000,"up":41200,"dq":0,"rc":"CONNECT_FAILED","len":0,"total":0,"co":5040})",
     withCo(failed(xweb(), "FAILED", "CONNECT_FAILED")), false},
    {"http_404",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":404,"co":5040,"ct":"text/html","len":120,"total":120,"sha":"472ba4fc5b7cf83e9227a27cc6b3aef7c61fa5a7096a5bf8c69bb5fcaf5f981b"})",
     http404(), true},
    {"login",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"login","ph":"login","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"len":0,"total":300,"ps":200,"co":5040})",
     loginOk(), false},
    {"login_failed",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"login","ph":"login","mod":"http","i":1,"n":1,"off":0,"st":"FAILED","ts":1793402820000,"up":41200,"dq":0,"rc":"LOGIN_FAILED","ps":200,"len":0,"total":0})",
     withPs(failed(login(xweb()), "FAILED", "LOGIN_FAILED"), 200), false},
    {"login_rejected",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"login","ph":"login","mod":"http","i":1,"n":1,"off":0,"st":"REJECTED","ts":1793402820000,"up":41200,"dq":0,"rc":"WRITE_NOT_ALLOWED","rdt":"WRITES_DENIED","len":0,"total":0})",
     loginRejected(), true},
    {"login_then_data[0]",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"login","ph":"login","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"len":0,"total":300,"ps":200,"co":5040})",
     loginOk(), false},
    {"login_then_data[1]",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"len":175,"total":175,"sha":"c66b3898918243d4edef7f6d45f54a8cc3662c7742cf00ede8eec1f0008bb693"})",
     xwebData(175, SHA_23), true},
    {"modbus_batch[0]",
     R"({"v":1,"job":"mb_meters","rev":1,"set":3,"src":"src_9c1d0b44","boot":"a1b2c3d4e5f60718","seq":1,"step":"u3","ph":"step","mod":"modbus_tcp","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"addr":"u3/hr3000","cnt":60,"len":120,"total":120,"sha":"a7d2bebab786e8bca9f2cb7766ac4b2174b9be9c785c6cfc17f650d76933590d"})",
     body(modbus("u3", "u3/hr3000", 60), 120, "a7d2bebab786e8bca9f2cb7766ac4b2174b9be9c785c6cfc17f650d76933590d"), true},
    {"modbus_batch[1]",
     R"({"v":1,"job":"mb_meters","rev":1,"set":3,"src":"src_9c1d0b44","boot":"a1b2c3d4e5f60718","seq":1,"step":"u4","ph":"step","mod":"modbus_tcp","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"addr":"u4/hr3000","cnt":60,"len":120,"total":120,"sha":"e082ae2b90cd9331e44e267b28a0dc12d5aa01b4e7cb2c0c8f2fa562b343fa9a"})",
     body(modbus("u4", "u4/hr3000", 60), 120, "e082ae2b90cd9331e44e267b28a0dc12d5aa01b4e7cb2c0c8f2fa562b343fa9a"), true},
    {"modbus_batch[2]",
     R"({"v":1,"job":"mb_meters","rev":1,"set":3,"src":"src_9c1d0b44","boot":"a1b2c3d4e5f60718","seq":1,"step":"u5","ph":"step","mod":"modbus_tcp","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"addr":"u5/ir0","cnt":40,"len":80,"total":80,"sha":"41211b92766722a8b6d29b915df5ad9113d296a2fbb9061aa7e42f2bf8ba7c43"})",
     body(modbus("u5", "u5/ir0", 40), 80, "41211b92766722a8b6d29b915df5ad9113d296a2fbb9061aa7e42f2bf8ba7c43"), true},
    {"modbus_exception",
     R"({"v":1,"job":"mb_meters","rev":1,"set":3,"src":"src_9c1d0b44","boot":"a1b2c3d4e5f60718","seq":1,"step":"u4","ph":"step","mod":"modbus_tcp","i":1,"n":1,"off":0,"st":"FAILED","ts":1793402820000,"up":41200,"dq":0,"ps":2,"addr":"u4/hr3000","cnt":60,"rc":"MODBUS_EXCEPTION","len":0,"total":0})",
     withPs(failed(modbus("u4", "u4/hr3000", 60), "FAILED", "MODBUS_EXCEPTION"), 2), true},
    {"modbus_registers",
     R"({"v":1,"job":"mb_meters","rev":1,"set":3,"src":"src_9c1d0b44","boot":"a1b2c3d4e5f60718","seq":1,"step":"u3","ph":"step","mod":"modbus_tcp","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"addr":"u3/hr3000","cnt":2,"len":4,"total":4,"sha":"9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a"})",
     body(modbus("u3", "u3/hr3000", 2), 4, SHA_REGS), true},
    {"modbus_timeout",
     R"({"v":1,"job":"mb_meters","rev":1,"set":3,"src":"src_9c1d0b44","boot":"a1b2c3d4e5f60718","seq":1,"step":"u3","ph":"step","mod":"modbus_tcp","i":1,"n":1,"off":0,"st":"FAILED","ts":1793402820000,"up":41200,"dq":0,"addr":"u3/hr3000","cnt":4,"rc":"TIMEOUT","len":0,"total":0})",
     failed(modbus("u3", "u3/hr3000", 4), "FAILED", "TIMEOUT"), true},
    {"modbus_ts_null",
     R"({"v":1,"job":"mb_meters","rev":1,"set":3,"src":"src_9c1d0b44","boot":"a1b2c3d4e5f60718","seq":1,"step":"u3","ph":"step","mod":"modbus_tcp","i":1,"n":1,"off":0,"st":"OK","ts":null,"up":41200,"dq":0,"addr":"u3/hr3000","cnt":2,"len":4,"total":4,"sha":"9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a"})",
     tsNull(), true},
    {"session_expired",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"FAILED","ts":1793402820000,"up":41200,"dq":0,"rc":"SESSION_EXPIRED","ps":401,"len":0,"total":0,"co":5040})",
     withCo(withPs(failed(xweb(), "FAILED", "SESSION_EXPIRED"), 401)), false},
    {"xweb_datachart_device_23",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"len":175,"total":175,"sha":"c66b3898918243d4edef7f6d45f54a8cc3662c7742cf00ede8eec1f0008bb693"})",
     xwebData(175, SHA_23), true},
    {"xweb_datachart_devices_13_17",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"len":913,"total":913,"sha":"4a005d592e0492f004a0150c0a35e115e15c8caf88c6831506c22820a949c3f2"})",
     xwebData(913, SHA_13_17), true},
    {"xweb_datachart_devices_18_22",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"len":913,"total":913,"sha":"805d7e369ce7973946dbe5cb5bdde7a839b949121642c8fcfdde21d31125919d"})",
     xwebData(913, "805d7e369ce7973946dbe5cb5bdde7a839b949121642c8fcfdde21d31125919d"), true},
    {"xweb_datachart_devices_1_7",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"len":958,"total":958,"sha":"12d74052d8039c6598935d83e90bec9352704b827de438277a6330e66cc854ac"})",
     xwebData(958, SHA_1_7), true},
    {"xweb_datachart_devices_8_12",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"len":911,"total":911,"sha":"4f2451c7bb169c9603f9dcb2888503c2abd5116ea7f2263542bdd8a5220a8cd2"})",
     xwebData(911, "4f2451c7bb169c9603f9dcb2888503c2abd5116ea7f2263542bdd8a5220a8cd2"), true},
    {"xweb_date_carried",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"date":"Fri, 30 Oct 2026 22:03:00 GMT","len":913,"total":913,"sha":"4a005d592e0492f004a0150c0a35e115e15c8caf88c6831506c22820a949c3f2"})",
     dateCarried(), true},
    {"xweb_expired_session_page",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"text/html","win":[1793396880,1793397780],"tr":true,"len":4096,"total":4096,"sha":"0fca97f8396f951e3827c051bfd3507a8981f1437ba2ad2f8358a6e03d49c62d"})",
     truncatedData(4096, "0fca97f8396f951e3827c051bfd3507a8981f1437ba2ad2f8358a6e03d49c62d", "text/html"), true},
    {"xweb_synthetic_empty",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"len":4,"total":4,"sha":"e3566b3a06430868d71e9287dfd6c6c520a3da027aabea01951d407ee131dc2f"})",
     xwebData(4, "e3566b3a06430868d71e9287dfd6c6c520a3da027aabea01951d407ee131dc2f"), true},
    {"xweb_synthetic_malformed",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"len":51,"total":51,"sha":"fdfbb22535c8005700fc14a2af341c1482546dd17bfa91c46c26fd724ab50d44"})",
     xwebData(51, "fdfbb22535c8005700fc14a2af341c1482546dd17bfa91c46c26fd724ab50d44"), true},
    {"xweb_synthetic_probe_error",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"len":141,"total":141,"sha":"61011e16adc86a6cb18ffe21cbcf89789975cc82281d4f3fa6f19382ffa8ad3f"})",
     xwebData(141, "61011e16adc86a6cb18ffe21cbcf89789975cc82281d4f3fa6f19382ffa8ad3f"), true},
    {"xweb_synthetic_truncated",
     R"({"v":1,"job":"xweb_hist","rev":1,"set":8,"src":"src_7f3a91c2","boot":"a1b2c3d4e5f60718","seq":1,"step":"chart","ph":"step","mod":"http","i":1,"n":1,"off":0,"st":"OK","ts":1793402820000,"up":41200,"dq":0,"ps":200,"co":5040,"ct":"application/json","win":[1793396880,1793397780],"tr":true,"len":500,"total":500,"sha":"28c371733fd985ecdd01f5effc7a68eee2f37f17b2b0eabb40191dd3f55f8b24"})",
     truncatedData(500, "28c371733fd985ecdd01f5effc7a68eee2f37f17b2b0eabb40191dd3f55f8b24", "application/json"), true},
};

// Flat JSON object -> (key, raw value token) pairs. Enough for part headers:
// string, number, true/false/null and flat arrays.
struct Field {
    char key[16];
    char value[96];
};

size_t parseFlat(const char *json, Field *fields, size_t cap) {
    size_t n = 0;
    const char *p = json;
    if (*p++ != '{') return 0;
    while (*p != '\0' && *p != '}' && n < cap) {
        if (*p == ',') p++;
        if (*p++ != '"') return 0;
        const char *k = p;
        while (*p != '"' && *p != '\0') p++;
        snprintf(fields[n].key, sizeof(fields[n].key), "%.*s", static_cast<int>(p - k), k);
        p += 2; // `":`
        const char *v = p;
        if (*p == '"') {
            p++;
            while (*p != '"' && *p != '\0') p += (*p == '\\') ? 2 : 1;
            p++;
        } else if (*p == '[') {
            while (*p != ']' && *p != '\0') p++;
            p++;
        } else {
            while (*p != ',' && *p != '}' && *p != '\0') p++;
        }
        snprintf(fields[n].value, sizeof(fields[n].value), "%.*s", static_cast<int>(p - v), v);
        n++;
    }
    return n;
}

const Field *findField(const Field *fields, size_t count, const char *key) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(fields[i].key, key) == 0) return &fields[i];
    }
    return nullptr;
}

} // namespace

static void test_part_header_goldens(void) {
    size_t exact = 0;
    for (const Golden &g : GOLDENS) {
        char out[PART_HEADER_MAX + 1];
        size_t length = encodePartHeader(g.header, out, sizeof(out));
        TEST_ASSERT_TRUE_MESSAGE(length > 0, g.name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(out), length, g.name);
        Field want[40], got[40];
        size_t wantCount = parseFlat(g.line, want, 40);
        size_t gotCount = parseFlat(out, got, 40);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(wantCount, gotCount, g.name);
        for (size_t i = 0; i < wantCount; i++) {
            const Field *f = findField(got, gotCount, want[i].key);
            char message[160];
            snprintf(message, sizeof(message), "%s: %s", g.name, want[i].key);
            TEST_ASSERT_NOT_NULL_MESSAGE(f, message);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].value, f->value, message);
        }
        if (g.exact) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(g.line, out, g.name);
            exact++;
        }
    }
    TEST_ASSERT_EQUAL_size_t(22, exact);
}

static void test_part_header_spec_fields_dur_and_co(void) {
    // The device sets co and dur on every part (spec); the goldens omit dur.
    PartHeader h = body(modbus("u3", "u3/hr3000", 2), 4, SHA_REGS);
    h.hasCo = true;
    h.co = 0;
    h.hasDur = true;
    h.durMs = 37;
    char out[PART_HEADER_MAX + 1];
    TEST_ASSERT_TRUE(encodePartHeader(h, out, sizeof(out)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, ",\"co\":0,\"addr\":"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"dur\":37}"));
}

static void test_part_header_omission_rules(void) {
    char out[PART_HEADER_MAX + 1];
    // date: data parts only, never on login parts.
    PartHeader data = dateCarried();
    TEST_ASSERT_TRUE(encodePartHeader(data, out, sizeof(out)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"date\":\"Fri, 30 Oct 2026 22:03:00 GMT\""));
    PartHeader loginPart = loginOk();
    loginPart.date = "Fri, 30 Oct 2026 22:03:00 GMT";
    TEST_ASSERT_TRUE(encodePartHeader(loginPart, out, sizeof(out)) > 0);
    TEST_ASSERT_NULL(strstr(out, "\"date\""));
    // Oversized (60 bytes) or non-printable Date is omitted; 40 bytes is kept.
    data.date = "Thu, 31 Oct 2026 12:00:00 GMT plus thirty extra bytes here!";
    TEST_ASSERT_TRUE(strlen(data.date) > 40);
    TEST_ASSERT_TRUE(encodePartHeader(data, out, sizeof(out)) > 0);
    TEST_ASSERT_NULL(strstr(out, "\"date\""));
    data.date = "Thu, 31 Oct 2026\t12:00:00 GMT";
    encodePartHeader(data, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "\"date\""));
    data.date = "0123456789012345678901234567890123456789";
    encodePartHeader(data, out, sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"date\":\"0123456789012345678901234567890123456789\""));
    // date and ct never on modbus parts.
    PartHeader mb = body(modbus("u3", "u3/hr3000", 2), 4, SHA_REGS);
    mb.date = "Fri, 30 Oct 2026 22:03:00 GMT";
    mb.ct = "application/octet-stream";
    encodePartHeader(mb, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "\"date\""));
    TEST_ASSERT_NULL(strstr(out, "\"ct\""));
    // ct over 64 bytes is omitted; a quote is escaped.
    PartHeader http = xwebData(10, SHA_23);
    http.ct = "application/x-www-form-urlencoded; charset=utf-8; boundary=abcdefg";
    TEST_ASSERT_TRUE(strlen(http.ct) > 64);
    encodePartHeader(http, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "\"ct\""));
    http.ct = "text/plain; x=\"y\"";
    encodePartHeader(http, out, sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"ct\":\"text/plain; x=\\\"y\\\"\""));
    // rdt: only on parts that are not OK and only for a code token.
    PartHeader rejected = loginRejected();
    rejected.rdt = "The secret is bound to another target";
    encodePartHeader(rejected, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "\"rdt\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"rc\":\"WRITE_NOT_ALLOWED\""));
    PartHeader ok = xwebData(10, SHA_23);
    ok.rc = "TIMEOUT";
    ok.rdt = "WRITES_DENIED";
    encodePartHeader(ok, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "\"rdt\""));
    TEST_ASSERT_NULL(strstr(out, "\"rc\""));
    // sha: never on header-only parts or login parts.
    PartHeader empty = xweb();
    empty.sha = SHA_23;
    encodePartHeader(empty, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "\"sha\""));
    PartHeader loginBody = loginOk();
    loginBody.sha = SHA_23;
    loginBody.len = 10;
    encodePartHeader(loginBody, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "\"sha\""));
    // rd only when true.
    PartHeader rd = xwebData(10, SHA_23);
    encodePartHeader(rd, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "\"rd\""));
    rd.rd = true;
    encodePartHeader(rd, out, sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"rd\":true"));
    // A string field outside printable ASCII refuses the header.
    PartHeader bad = xweb();
    bad.step = "a\nb";
    TEST_ASSERT_EQUAL_size_t(0, encodePartHeader(bad, out, sizeof(out)));
    // Too small an output buffer.
    char tiny[32];
    TEST_ASSERT_EQUAL_size_t(0, encodePartHeader(xweb(), tiny, sizeof(tiny)));
}

static void test_part_header_max_fits_768(void) {
    const char *id = "abcdefghijklmnopqrstuvwxyz012345";
    TEST_ASSERT_EQUAL_size_t(32, strlen(id));
    PartHeader h{};
    h.job = id;
    h.rev = UINT32_MAX;
    h.set = UINT32_MAX;
    h.src = "src_ffffffff";
    h.boot = BOOT;
    h.seq = UINT32_MAX;
    h.step = id;
    h.module = Module::HTTPS;
    h.i = UINT32_MAX;
    h.n = UINT32_MAX;
    h.off = UINT32_MAX;
    h.len = UINT32_MAX;
    h.total = UINT32_MAX;
    h.sha = SHA_23;
    h.tr = true;
    h.st = "OK";
    h.hasPs = true;
    h.ps = 599;
    h.tsKnown = true;
    h.tsMs = UINT64_MAX;
    h.upMs = UINT64_MAX;
    h.hasDur = true;
    h.durMs = UINT32_MAX;
    h.hasCo = true;
    h.co = -604800;
    h.hasWin = true;
    h.winFrom = INT64_MIN;
    h.winTo = INT64_MAX;
    h.dq = UINT32_MAX;
    h.rd = true;
    h.ct = "application/vnd.energyme.collector.part+json; charset=utf-8; v=1";
    TEST_ASSERT_EQUAL_size_t(64, strlen(h.ct));
    h.date = "Thu, 31 Oct 2026 12:00:00 GMT+0000 (UTC)";
    TEST_ASSERT_EQUAL_size_t(40, strlen(h.date));
    char out[1024];
    size_t ok = encodePartHeader(h, out, sizeof(out));
    TEST_ASSERT_TRUE(ok > 0);
    TEST_ASSERT_TRUE(ok <= PART_HEADER_MAX);
    char message[64];
    snprintf(message, sizeof(message), "OK https data part at max: %u bytes", static_cast<unsigned>(ok));
    TEST_MESSAGE(message);

    // A failed part at max: rc and rdt of 24 bytes, no sha, ct or date.
    h.st = "REJECTED";
    h.rc = "ABCDEFGHIJKLMNOPQRSTUVWX";
    h.rdt = "ABCDEFGHIJKLMNOPQRSTUVWX";
    h.sha = nullptr;
    h.ct = nullptr;
    h.date = nullptr;
    size_t failedLength = encodePartHeader(h, out, sizeof(out));
    TEST_ASSERT_TRUE(failedLength > 0 && failedLength <= PART_HEADER_MAX);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"rdt\":\"ABCDEFGHIJKLMNOPQRSTUVWX\""));

    // A Modbus part at max.
    h.module = Module::MODBUS_TCP;
    h.addr = "u255/hr65535";
    h.cnt = 125;
    size_t mbLength = encodePartHeader(h, out, sizeof(out));
    TEST_ASSERT_TRUE(mbLength > 0 && mbLength <= PART_HEADER_MAX);
}

static void test_code_token(void) {
    TEST_ASSERT_TRUE(isCodeToken("WRITES_DENIED"));
    TEST_ASSERT_TRUE(isCodeToken("ABCDEFGHIJKLMNOPQRSTUVWX"));
    TEST_ASSERT_FALSE(isCodeToken("ABCDEFGHIJKLMNOPQRSTUVWXY"));
    TEST_ASSERT_FALSE(isCodeToken(""));
    TEST_ASSERT_FALSE(isCodeToken(nullptr));
    TEST_ASSERT_FALSE(isCodeToken("Writes denied"));
    TEST_ASSERT_FALSE(isCodeToken("CODE1"));
}

static void test_chunk_counts(void) {
    TEST_ASSERT_EQUAL_UINT32(3, chunkCount(102400));
    TEST_ASSERT_EQUAL_size_t(4096, 102400 - 2 * CHUNK_BYTES);
    TEST_ASSERT_EQUAL_UINT32(1, chunkCount(0));
    TEST_ASSERT_EQUAL_UINT32(1, chunkCount(900));
    TEST_ASSERT_EQUAL_UINT32(1, chunkCount(49152));
    TEST_ASSERT_EQUAL_UINT32(2, chunkCount(49153));
    TEST_ASSERT_EQUAL_UINT32(3, chunkCount(131072));
}

static void test_pack_registers(void) {
    uint16_t regs[] = {0x0102, 0x0304};
    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(4, packRegisters(regs, 2, false, out, sizeof(out)));
    const uint8_t expected[] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 4);
    uint16_t bits[] = {1, 0, 1, 1, 0, 0, 0, 0, 1};
    TEST_ASSERT_EQUAL_size_t(2, packRegisters(bits, 9, true, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x0D, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[1]);
    TEST_ASSERT_EQUAL_size_t(0, packRegisters(regs, 2, false, out, 3));
    uint16_t sixty[60];
    for (uint16_t i = 0; i < 60; i++) sixty[i] = i;
    uint8_t batch[120];
    TEST_ASSERT_EQUAL_size_t(120, packRegisters(sixty, 60, false, batch, sizeof(batch)));
}

// ---------------------------------------------------------------------------
// Out-queue
// ---------------------------------------------------------------------------

namespace {

struct Notes {
    size_t dropped;
    size_t published;
    PartTag last[64];
    bool lastPublished[64];
    size_t count;
};

void note(const PartTag &tag, bool published, void *context) {
    Notes *n = static_cast<Notes *>(context);
    if (published) {
        n->published++;
    } else {
        n->dropped++;
    }
    if (n->count < 64) {
        n->last[n->count] = tag;
        n->lastPublished[n->count] = published;
        n->count++;
    }
}

uint8_t ring[16384];
uint8_t payload[12000];

bool pushPart(PartQueue &q, const char *src, uint8_t job, uint32_t seq, uint32_t bodyNo, size_t bytes, Notes *notes) {
    char header[64];
    int n = snprintf(header, sizeof(header), "{\"job\":%u,\"seq\":%u,\"body\":%u}", job, static_cast<unsigned>(seq), static_cast<unsigned>(bodyNo));
    memset(payload, static_cast<int>(bodyNo & 0xFF), bytes);
    return q.push(src, PartTag{job, seq, bodyNo}, header, static_cast<size_t>(n), payload, bytes, note, notes);
}

} // namespace

static void test_queue_drops_whole_bodies_oldest_first(void) {
    PartQueue q;
    q.init(ring, 12000);
    Notes notes{};
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 1, 1, 3000, &notes)); // body 1, chunk 1
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 1, 1, 3000, &notes)); // body 1, chunk 2
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 1, 2, 3000, &notes)); // body 2
    TEST_ASSERT_EQUAL_size_t(3, q.parts());
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 1, 1, 3, 3000, &notes)); // needs room: both chunks of body 1 go
    TEST_ASSERT_EQUAL_size_t(2, notes.dropped);
    TEST_ASSERT_EQUAL_UINT32(1, notes.last[0].body);
    TEST_ASSERT_EQUAL_UINT32(1, notes.last[1].body);
    TEST_ASSERT_EQUAL_UINT32(2, q.droppedParts());
    TEST_ASSERT_EQUAL_size_t(2, q.parts());
    PartQueue::View view{};
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_NOT_NULL(strstr(reinterpret_cast<const char *>(view.parts[0]), "\"body\":2"));
    q.endMessage(true, note, &notes);
    TEST_ASSERT_EQUAL_size_t(1, notes.published);
}

static void test_queue_pinned_never_evicted(void) {
    PartQueue q;
    q.init(ring, 16000);
    Notes notes{};
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 1, 1, 6000, &notes));
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 1, 2, 6000, &notes));
    PartQueue::View view{};
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_size_t(1, view.count); // larger than 5120: travels alone
    static uint8_t snapshot[7000];
    const uint8_t *pinned = view.parts[0];
    size_t pinnedBytes = view.partBytes[0];
    memcpy(snapshot, pinned, pinnedBytes);
    // Needs eviction: body 2 goes, the pinned body 1 stays and does not move.
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 2, 3, 6000, &notes));
    TEST_ASSERT_EQUAL_size_t(1, notes.dropped);
    TEST_ASSERT_EQUAL_UINT32(2, notes.last[0].body);
    TEST_ASSERT_EQUAL_MEMORY(snapshot, pinned, pinnedBytes);
    // Only the pinned part could make room: the new part is dropped and notified.
    TEST_ASSERT_FALSE(pushPart(q, "src_a1b2", 0, 3, 4, 10000, &notes));
    TEST_ASSERT_EQUAL_size_t(2, q.parts()); // body 3 was not evicted for nothing
    TEST_ASSERT_EQUAL_size_t(2, notes.dropped);
    TEST_ASSERT_EQUAL_UINT32(4, notes.last[1].body);
    TEST_ASSERT_EQUAL_MEMORY(snapshot, pinned, pinnedBytes);
    // Publish failure: unpinned, still queued, same head next time.
    q.endMessage(false, note, &notes);
    TEST_ASSERT_EQUAL_size_t(0, notes.published);
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_MEMORY(snapshot, view.parts[0], pinnedBytes);
    q.endMessage(true, note, &notes);
    TEST_ASSERT_EQUAL_size_t(1, notes.published);
    TEST_ASSERT_EQUAL_UINT32(1, notes.last[2].body);
}

static void test_queue_started_body_dropped_whole(void) {
    PartQueue q;
    q.init(ring, 16000);
    Notes notes{};
    // A 3-chunk body; chunk 1 is published, then the queue fills up.
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 1, 7, 5200, &notes));
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 1, 7, 5200, &notes));
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 1, 7, 5200, &notes));
    PartQueue::View view{};
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_size_t(1, view.count);
    q.endMessage(true, note, &notes);
    // Chunk 2 in flight: chunk 3 of the same body is never evicted alone, so the new part is dropped.
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_FALSE(pushPart(q, "src_a1b2", 1, 1, 8, 6000, &notes));
    TEST_ASSERT_EQUAL_size_t(2, q.parts());
    TEST_ASSERT_EQUAL_size_t(1, notes.dropped);
    TEST_ASSERT_EQUAL_UINT32(8, notes.last[1].body);
    q.endMessage(false, note, &notes);
    // Nothing pinned: the started body goes whole (chunks 2 and 3 together), counted in dq.
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 1, 1, 9, 9000, &notes));
    TEST_ASSERT_EQUAL_size_t(3, notes.dropped);
    TEST_ASSERT_EQUAL_UINT32(7, notes.last[2].body);
    TEST_ASSERT_EQUAL_UINT32(7, notes.last[3].body);
    TEST_ASSERT_EQUAL_size_t(1, q.parts());
    TEST_ASSERT_EQUAL_UINT32(3, q.droppedParts());
}

static void test_queue_refuses_rest_of_dropped_body(void) {
    PartQueue q;
    q.init(ring, 12000);
    Notes notes{};
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 1, 1, 5000, &notes));
    PartQueue::View view{};
    TEST_ASSERT_TRUE(q.beginMessage(&view)); // pin body 1
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 2, 5, 3000, &notes));  // body 5 chunk 1
    TEST_ASSERT_FALSE(pushPart(q, "src_a1b2", 0, 2, 5, 6000, &notes)); // chunk 2 cannot fit: body 5 goes whole
    TEST_ASSERT_EQUAL_size_t(1, q.parts());
    TEST_ASSERT_EQUAL_size_t(2, notes.dropped);
    TEST_ASSERT_FALSE(pushPart(q, "src_a1b2", 0, 2, 5, 100, &notes)); // chunk 3 refused, no partial body
    TEST_ASSERT_EQUAL_size_t(3, notes.dropped);
    TEST_ASSERT_TRUE(pushPart(q, "src_a1b2", 0, 2, 6, 100, &notes));
}

static void test_queue_coalesces_by_source(void) {
    PartQueue q;
    q.init(ring, sizeof(ring));
    Notes notes{};
    TEST_ASSERT_TRUE(pushPart(q, "src_9c1d", 0, 1, 1, 1500, &notes));
    TEST_ASSERT_TRUE(pushPart(q, "src_9c1d", 0, 1, 2, 1500, &notes));
    TEST_ASSERT_TRUE(pushPart(q, "src_9c1d", 0, 1, 3, 1500, &notes));
    TEST_ASSERT_TRUE(pushPart(q, "src_9c1d", 0, 1, 4, 1500, &notes)); // would pass 5120
    TEST_ASSERT_TRUE(pushPart(q, "src_7f3a", 1, 1, 5, 100, &notes));
    PartQueue::View view{};
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_STRING("src_9c1d", view.sourceId);
    TEST_ASSERT_EQUAL_size_t(3, view.count);
    TEST_ASSERT_TRUE(view.bytes <= COALESCE_BYTES);
    size_t sum = 0;
    for (size_t i = 0; i < view.count; i++) {
        sum += view.partBytes[i];
        TEST_ASSERT_EQUAL_UINT8('\n', view.parts[i][view.headerLength[i]]);
    }
    TEST_ASSERT_EQUAL_size_t(view.bytes, sum);
    q.endMessage(true, note, &notes);
    TEST_ASSERT_EQUAL_size_t(3, notes.published);
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_size_t(1, view.count); // stops at the other source
    q.endMessage(true, note, &notes);
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_STRING("src_7f3a", view.sourceId);
    q.endMessage(true, note, &notes);
    TEST_ASSERT_FALSE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_size_t(0, q.bytes());
}

static void test_queue_small_modbus_batch_one_message(void) {
    PartQueue q;
    q.init(ring, sizeof(ring));
    Notes notes{};
    // Three parts of 120 raw bytes plus ~330-byte headers: one message.
    char header[340];
    memset(header, 'h', sizeof(header));
    uint8_t regs[120] = {};
    for (uint32_t b = 1; b <= 3; b++) TEST_ASSERT_TRUE(q.push("src_9c1d", PartTag{0, 1, b}, header, sizeof(header), regs, sizeof(regs), note, &notes));
    PartQueue::View view{};
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_size_t(3, view.count);
    TEST_ASSERT_EQUAL_size_t(3 * (340 + 1 + 120), view.bytes);
    // At most 16 parts per message.
    q.endMessage(true, note, &notes);
    for (uint32_t b = 1; b <= 20; b++) TEST_ASSERT_TRUE(q.push("src_9c1d", PartTag{0, 2, b}, "{}", 2, regs, 10, note, &notes));
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_size_t(16, view.count);
    q.endMessage(true, note, &notes);
}

// A chunk of a multi-chunk body is published as its own message, never coalesced.
static void test_queue_chunk_travels_alone(void) {
    PartQueue q;
    q.init(ring, sizeof(ring));
    Notes notes{};
    uint8_t regs[100] = {};
    TEST_ASSERT_TRUE(q.push("src_9c1d", PartTag{0, 1, 1}, "{}", 2, regs, sizeof(regs), note, &notes));
    TEST_ASSERT_TRUE(q.push("src_9c1d", PartTag{0, 1, 2, 0, true}, "{}", 2, regs, sizeof(regs), note, &notes));
    TEST_ASSERT_TRUE(q.push("src_9c1d", PartTag{0, 1, 3}, "{}", 2, regs, sizeof(regs), note, &notes));
    PartQueue::View view{};
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_size_t(1, view.count);
    q.endMessage(true, note, &notes);
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_size_t(1, view.count); // the chunk
    q.endMessage(true, note, &notes);
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_size_t(1, view.count);
    q.endMessage(true, note, &notes);
}

// The same job index, seq and body of another table generation is another body.
static void test_queue_generation_separates_bodies(void) {
    PartQueue q;
    q.init(ring, sizeof(ring));
    Notes notes{};
    uint8_t regs[10] = {};
    TEST_ASSERT_TRUE(q.push("src_9c1d", PartTag{0, 1, 1, 1}, "{}", 2, regs, sizeof(regs), note, &notes));
    TEST_ASSERT_TRUE(q.push("src_9c1d", PartTag{0, 1, 1, 2}, "{}", 2, regs, sizeof(regs), note, &notes));
    PartQueue::View view{};
    TEST_ASSERT_TRUE(q.beginMessage(&view));
    TEST_ASSERT_EQUAL_size_t(2, view.count);
    q.endMessage(true, note, &notes);
    TEST_ASSERT_EQUAL_size_t(0, q.parts());
}

static void test_queue_without_storage(void) {
    PartQueue q;
    TEST_ASSERT_FALSE(q.ready());
    Notes notes{};
    TEST_ASSERT_FALSE(pushPart(q, "src_a1b2", 0, 1, 1, 10, &notes));
    TEST_ASSERT_EQUAL_size_t(1, notes.dropped);
    PartQueue::View view{};
    TEST_ASSERT_FALSE(q.beginMessage(&view));
}

// ---------------------------------------------------------------------------
// Token bucket
// ---------------------------------------------------------------------------

static void test_token_bucket(void) {
    TokenBucket bucket;
    bucketInit(&bucket, 3600, 0); // 1 byte per second
    TEST_ASSERT_TRUE(bucketHasTokens(bucket));
    bucketSpend(&bucket, 8000); // a whole message goes out, the bucket goes into debt
    TEST_ASSERT_FALSE(bucketHasTokens(bucket));
    TEST_ASSERT_TRUE(bucket.tokens == -3600.0); // debt bounded by one hour
    bucketRefill(&bucket, 3600000);
    TEST_ASSERT_FALSE(bucketHasTokens(bucket)); // back at 0
    bucketRefill(&bucket, 3610000);
    TEST_ASSERT_TRUE(bucketHasTokens(bucket));
    TEST_ASSERT_TRUE(bucket.tokens == 10.0);
    bucketRefill(&bucket, 100000000);
    TEST_ASSERT_TRUE(bucket.tokens == 3600.0); // capped at the hourly limit

    TokenBucket big;
    bucketInit(&big, BUDGET_DEFAULT, 0);
    bucketSpend(&big, BUDGET_DEFAULT);
    TEST_ASSERT_FALSE(bucketHasTokens(big)); // runs wait with BUDGET
    bucketRefill(&big, 1000);
    TEST_ASSERT_TRUE(bucketHasTokens(big)); // ~582 B/s refill
}

// ---------------------------------------------------------------------------
// Health
// ---------------------------------------------------------------------------

static void test_health_err_after_three_failures(void) {
    Health h{};
    TEST_ASSERT_TRUE(healthOnRun(&h, true, "CONNECT_FAILED", nullptr, 100));
    TEST_ASSERT_EQUAL_STRING("ok", healthStateName(h.st));
    TEST_ASSERT_EQUAL_STRING("CONNECT_FAILED", h.code);
    TEST_ASSERT_EQUAL_UINT64(100, h.since);
    TEST_ASSERT_FALSE(healthOnRun(&h, true, "CONNECT_FAILED", nullptr, 200));
    TEST_ASSERT_TRUE(healthOnRun(&h, true, "CONNECT_FAILED", nullptr, 300));
    TEST_ASSERT_EQUAL_STRING("err", healthStateName(h.st));
    TEST_ASSERT_EQUAL_UINT64(300, h.since);
    TEST_ASSERT_EQUAL_UINT32(3, h.consecutiveFailures);
}

static void test_health_ok_after_one_success_clears(void) {
    Health h{};
    for (int i = 0; i < 3; i++) healthOnRun(&h, true, "WRITE_NOT_ALLOWED", "WRITES_DENIED", 10);
    TEST_ASSERT_TRUE(h.st == HealthState::ERR);
    TEST_ASSERT_EQUAL_STRING("WRITES_DENIED", h.detail);
    TEST_ASSERT_TRUE(healthOnRun(&h, false, nullptr, nullptr, 500));
    TEST_ASSERT_TRUE(h.st == HealthState::OK);
    TEST_ASSERT_EQUAL_STRING("", h.code);
    TEST_ASSERT_EQUAL_STRING("", h.detail);
    TEST_ASSERT_EQUAL_UINT32(0, h.consecutiveFailures);
    TEST_ASSERT_EQUAL_UINT64(500, h.since);
}

static void test_health_partial_run_and_detail(void) {
    Health h{};
    // Successful run with one silent Modbus unit: st ok, code TIMEOUT.
    TEST_ASSERT_TRUE(healthOnRun(&h, false, "TIMEOUT", nullptr, 50));
    TEST_ASSERT_TRUE(h.st == HealthState::OK);
    TEST_ASSERT_EQUAL_STRING("TIMEOUT", h.code);
    // detail only for a code token.
    healthOnRun(&h, true, "SECRET_TARGET_MISMATCH", "The secret is bound to another target", 60);
    TEST_ASSERT_EQUAL_STRING("", h.detail);
    TEST_ASSERT_TRUE(healthOnRun(&h, true, "SECRET_TARGET_MISMATCH", "MAC_REQUIRED", 70)); // detail change is a change
    TEST_ASSERT_EQUAL_STRING("MAC_REQUIRED", h.detail);
    TEST_ASSERT_EQUAL_UINT64(60, h.since); // since tracks st and code only
}

static void test_health_wait_and_off(void) {
    Health h{};
    TEST_ASSERT_TRUE(healthOnWait(&h, "WAIT_CLOCK", 5));
    TEST_ASSERT_EQUAL_STRING("wait", healthStateName(h.st));
    TEST_ASSERT_EQUAL_STRING("WAIT_CLOCK", h.code);
    TEST_ASSERT_FALSE(healthOnWait(&h, "WAIT_CLOCK", 6));
    TEST_ASSERT_TRUE(healthOnOff(&h, "GATEWAY_DISABLED", 7));
    TEST_ASSERT_EQUAL_STRING("off", healthStateName(h.st));
    TEST_ASSERT_TRUE(healthOnOff(&h, nullptr, 8));
    TEST_ASSERT_EQUAL_STRING("", h.code);
    // A failed run after a wait leaves wait: ok with the code until the third failure.
    TEST_ASSERT_TRUE(healthOnRun(&h, true, "TIMEOUT", nullptr, 9));
    TEST_ASSERT_TRUE(h.st == HealthState::OK);
    TEST_ASSERT_EQUAL_STRING("ok", healthStateName(HealthState::OK));
    TEST_ASSERT_EQUAL_STRING("err", healthStateName(HealthState::ERR));
}

static void test_report_coalescing(void) {
    // Flapping target: health-only changes at most once per 300 s.
    TEST_ASSERT_FALSE(reportDue(true, false, 1000, 1060));
    TEST_ASSERT_FALSE(reportDue(true, false, 1000, 1299));
    TEST_ASSERT_TRUE(reportDue(true, false, 1000, 1300));
    TEST_ASSERT_FALSE(reportDue(false, false, 1000, 5000));
    // Apply results and switch changes at once.
    TEST_ASSERT_TRUE(reportDue(false, true, 1000, 1001));
}

void run_collector_runtime_tests(void) {
    RUN_TEST(test_backoff_growth_and_cap);
    RUN_TEST(test_first_slot_spread);
    RUN_TEST(test_no_burst_after_pause);
    RUN_TEST(test_window_normal_advance_with_overlap);
    RUN_TEST(test_window_outage_longer_than_backfill);
    RUN_TEST(test_window_truncated_halves_from_same_start);
    RUN_TEST(test_window_halving_and_doubling_bounds);
    RUN_TEST(test_window_clock_steps_back);
    RUN_TEST(test_source_clock);
    RUN_TEST(test_step_fates);
    RUN_TEST(test_part_header_goldens);
    RUN_TEST(test_part_header_spec_fields_dur_and_co);
    RUN_TEST(test_part_header_omission_rules);
    RUN_TEST(test_part_header_max_fits_768);
    RUN_TEST(test_code_token);
    RUN_TEST(test_chunk_counts);
    RUN_TEST(test_pack_registers);
    RUN_TEST(test_queue_drops_whole_bodies_oldest_first);
    RUN_TEST(test_queue_pinned_never_evicted);
    RUN_TEST(test_queue_started_body_dropped_whole);
    RUN_TEST(test_queue_refuses_rest_of_dropped_body);
    RUN_TEST(test_queue_coalesces_by_source);
    RUN_TEST(test_queue_small_modbus_batch_one_message);
    RUN_TEST(test_queue_without_storage);
    RUN_TEST(test_queue_chunk_travels_alone);
    RUN_TEST(test_queue_generation_separates_bodies);
    RUN_TEST(test_token_bucket);
    RUN_TEST(test_health_err_after_three_failures);
    RUN_TEST(test_health_ok_after_one_success_clears);
    RUN_TEST(test_health_partial_run_and_detail);
    RUN_TEST(test_health_wait_and_off);
    RUN_TEST(test_report_coalescing);
}
