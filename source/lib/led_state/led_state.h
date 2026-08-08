// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure, dependency-free logic for what the status LED shows.
//
// The LED is the only feedback channel on a headless meter, so "which indication
// wins" has to be one rule with one home. That rule is here: a fixed table of
// priority layers, each holding at most one indication, and a resolver that
// always returns the highest-priority occupied layer.
//
// The layer table replaces an arbitration queue in which a losing command was
// re-queued and retried forever. Once an indefinite critical indication was
// active nothing lower could ever win again, the queue filled at ten entries,
// and further commands were dropped without a trace - so the colour stopped
// matching the device state. A write into a layer cannot collide with another
// layer, which is what removes that whole failure class.
//
// The renderer is stateless on purpose. A pattern's waveform is a function of
// (nowMs - setAtMs) alone, so there is no retained "what is showing" copy that
// can drift from the table. See openspec design.md D2.
//
// Knows nothing about Arduino, FreeRTOS or NVS, which is what makes every rule
// below host-testable.

namespace LedState {

// Priority layers, lowest first. The numeric values ARE the priority order:
// resolve() scans from the highest value down and returns the first occupied slot.
//
// USER sits directly above STATUS, not at the bottom. STATUS is the ambient
// baseline - the firmware occupies it at boot and never releases it, so a user
// colour underneath could never be seen on a healthy device, which would make the
// whole API inert. Above STATUS it replaces the ambient indication and nothing
// else: every layer that carries an actual event still outranks it.
enum class Layer : uint8_t {
    STATUS = 0,    // Ambient status: boot stage, healthy. Always occupied.
    USER = 1,      // REST API / home automation
    NETWORK = 2,   // WiFi and connectivity
    ALERT = 3,     // Button feedback, updates, recoverable faults
    CRITICAL = 4,
    Count
};

constexpr uint8_t LAYER_COUNT = (uint8_t)Layer::Count;

// resolve() walks the slots with a signed index.
static_assert(LAYER_COUNT <= 127, "Layer indices must fit in int8_t");

enum class Pattern : uint8_t {
    OFF = 0,
    SOLID,
    BLINK_SLOW,     // 1000 ms on, 1000 ms off
    BLINK_FAST,     // 250 ms on, 250 ms off
    PULSE,          // 1000 ms fade up, 1000 ms fade down
    DOUBLE_BLINK,   // 100 on, 100 off, 100 on, 900 off
    Count
};

constexpr uint8_t PATTERN_COUNT = (uint8_t)Pattern::Count;

// Pattern periods. Exposed because the tests assert on segment boundaries, and a
// caller choosing a duration wants to know how long one cycle takes.
constexpr uint64_t BLINK_SLOW_HALF_MS = 1000;
constexpr uint64_t BLINK_FAST_HALF_MS = 250;
constexpr uint64_t PULSE_HALF_MS = 1000;
constexpr uint64_t DOUBLE_BLINK_SEGMENT_MS = 100;
constexpr uint64_t DOUBLE_BLINK_CYCLE_MS = 1200;

constexpr uint8_t MAX_BRIGHTNESS_PERCENT = 100;

// Per-layer render-time brightness floor, so an indication the user has to see is
// not silenced by a configured brightness of 0. Indexed by Layer, so a new layer
// cannot forget to state one.
//
// CRITICAL (safe mode, factory reset) gets a properly visible floor. ALERT (button
// feedback, updates) gets the barely-visible 1% the call sites were already asking
// for: enough to confirm a press in the dark without lighting the room.
//
// These are never persisted. The three sites that used to do
// `setBrightness(max(getBrightness(), 1))` were writing the floor into NVS, so a
// single button press permanently replaced a user's stored 0 with 1 - and left the
// whole device at 1% afterwards, not just the indication that needed it.
constexpr uint8_t LAYER_MIN_BRIGHTNESS_PERCENT[] = {
    0,   // STATUS
    0,   // USER
    0,   // NETWORK
    1,   // ALERT
    10,  // CRITICAL
};
static_assert(sizeof(LAYER_MIN_BRIGHTNESS_PERCENT) == LAYER_COUNT,
              "Every layer needs a brightness floor");

struct Rgb {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
};

inline bool operator==(const Rgb &a, const Rgb &b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}
inline bool operator!=(const Rgb &a, const Rgb &b) { return !(a == b); }

// Lives here rather than in the firmware header so the host tests can use the same
// palette the device does.
namespace Colors {
    constexpr Rgb RED{255, 0, 0};
    constexpr Rgb GREEN{0, 255, 0};
    constexpr Rgb BLUE{0, 0, 255};
    constexpr Rgb YELLOW{255, 255, 0};
    constexpr Rgb PURPLE{255, 0, 255};
    constexpr Rgb CYAN{0, 255, 255};
    constexpr Rgb ORANGE{255, 128, 0};
    constexpr Rgb WHITE{255, 255, 255};
    constexpr Rgb OFF{0, 0, 0};
}

struct Slot {
    bool occupied = false;
    Pattern pattern = Pattern::OFF;
    Rgb color;
    uint64_t setAtMs = 0;
    uint64_t durationMs = 0;  // 0 = indefinite
};

struct Table {
    Slot slots[LAYER_COUNT];
};

// What the LED is currently showing.
struct Active {
    bool any = false;           // false => every layer is free, LED is dark
    Layer layer = Layer::STATUS;  // meaningless unless `any`
    Pattern pattern = Pattern::OFF;
    Rgb color;
    uint64_t elapsedMs = 0;     // since the indication was set; drives the waveform
    uint64_t remainingMs = 0;   // 0 when indefinite - check `indefinite`
    bool indefinite = true;
};

// One render pass: what is showing, and what the pins should be driven to.
struct Frame {
    Active active;
    Rgb output;
    bool isOn = false;  // waveform is in an on phase (independent of the colour)
};

// Writes one indication into one layer, replacing whatever that layer held.
//
// Only the addressed slot is touched, so this can never evict, delay, reorder or
// drop another layer's indication. durationMs of 0 means indefinite.
//
// setAtMs restarts the waveform phase, which is deliberate: re-setting the same
// blink is how a repeated event stays visible as a fresh blink rather than
// disappearing into an already-running cycle.
void set(Table &table, Layer layer, Pattern pattern, Rgb color, uint64_t nowMs, uint64_t durationMs);

// Frees a layer, revealing the highest-priority layer still occupied. Releasing a
// free layer is a no-op. This is the operation the old queue could not express -
// its "clear" meant "turn the LED off", so every caller had to guess and re-assert
// what should come next.
void release(Table &table, Layer layer);

void releaseAll(Table &table);

// Advances the table to nowMs and renders it. The only entry point a caller needs:
// expiry, resolution, the per-layer brightness floor and the waveform are composed
// here, so no caller can get the order wrong or skip a step.
Frame step(Table &table, uint64_t nowMs, uint8_t configuredBrightnessPercent);

// The pieces step() is built from. Exposed for tests and for callers that only want
// one of them; prefer step() when you want a render pass.
bool expire(Table &table, uint64_t nowMs);          // frees elapsed slots, true if any
Active resolve(const Table &table, uint64_t nowMs); // does not expire - call expire() first
bool isOn(Pattern pattern, uint64_t elapsedMs);
Rgb render(Pattern pattern, Rgb color, uint64_t elapsedMs, uint8_t brightnessPercent);
uint8_t effectiveBrightness(Layer layer, uint8_t configuredPercent);

// Wire names for the REST API. Both directions live here so the mapping is stated
// once, and the tests can round-trip it.
const char *patternName(Pattern pattern);
const char *layerName(Layer layer);
bool patternFromName(const char *name, Pattern &out);

}  // namespace LedState
