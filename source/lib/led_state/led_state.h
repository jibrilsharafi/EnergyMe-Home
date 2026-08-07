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
enum class Layer : uint8_t {
    USER = 0,      // REST API / home automation. Lowest, so status always wins.
    STATUS = 1,    // Normal operating status: boot stage, healthy, error
    NETWORK = 2,   // WiFi and connectivity
    ALERT = 3,     // Button feedback, updates, recoverable faults
    CRITICAL = 4   // Safe mode, factory reset, unrecoverable faults
};

constexpr uint8_t LAYER_COUNT = 5;

enum class Pattern : uint8_t {
    OFF = 0,
    SOLID,
    BLINK_SLOW,     // 1000 ms on, 1000 ms off
    BLINK_FAST,     // 250 ms on, 250 ms off
    PULSE,          // 1000 ms fade up, 1000 ms fade down
    DOUBLE_BLINK    // 100 on, 100 off, 100 on, 900 off
};

// Pattern periods. Exposed because the tests assert on segment boundaries, and a
// caller choosing a duration wants to know how long one cycle takes.
#define LED_STATE_BLINK_SLOW_HALF_MS 1000ULL
#define LED_STATE_BLINK_FAST_HALF_MS 250ULL
#define LED_STATE_PULSE_HALF_MS 1000ULL
#define LED_STATE_DOUBLE_BLINK_SEGMENT_MS 100ULL
#define LED_STATE_DOUBLE_BLINK_CYCLE_MS 1200ULL

// Render-time brightness floors, so an indication the user has to see is not
// silenced by a configured brightness of 0. CRITICAL (safe mode, factory reset)
// gets a properly visible floor; ALERT (button feedback, updates) gets the barely
// visible 1% that the call sites were already asking for.
//
// These are never persisted. The three sites that used to do
// `setBrightness(max(getBrightness(), 1))` were writing the floor into NVS, so a
// single button press permanently replaced a user's stored 0 with 1 - and left the
// whole device at 1% afterwards, not just the indication that needed it.
#define LED_STATE_CRITICAL_MIN_BRIGHTNESS_PERCENT 10
#define LED_STATE_ALERT_MIN_BRIGHTNESS_PERCENT 1

#define LED_STATE_MAX_BRIGHTNESS_PERCENT 100

struct Rgb {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    Rgb() = default;
    // Kept so the firmware's Led::Color can be an alias of this rather than a
    // second colour type that has to be converted at every boundary.
    Rgb(uint8_t r, uint8_t g, uint8_t b) : red(r), green(g), blue(b) {}
};

inline bool operator==(const Rgb &a, const Rgb &b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}
inline bool operator!=(const Rgb &a, const Rgb &b) { return !(a == b); }

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
    Layer layer = Layer::USER;  // meaningless unless `any`
    Pattern pattern = Pattern::OFF;
    Rgb color;
    uint64_t elapsedMs = 0;     // since the indication was set; drives the waveform
    uint64_t remainingMs = 0;   // 0 when indefinite - check `indefinite`
    bool indefinite = true;
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

// Frees every layer whose duration has elapsed. Returns true if anything was freed.
//
// Expiry is measured from setAtMs, so an indication that was masked by a higher
// layer for its whole lifetime is already gone when that higher layer is released,
// rather than starting its countdown late.
bool expire(Table &table, uint64_t nowMs);

// Highest-priority occupied layer. Does not apply expiry - call expire() first.
Active resolve(const Table &table, uint64_t nowMs);

// Colour to drive the pins with. brightnessPercent is applied exactly once, here;
// no caller may scale the result again.
Rgb render(Pattern pattern, Rgb color, uint64_t elapsedMs, uint8_t brightnessPercent);

// Brightness to render `layer` at, given what the user configured.
uint8_t effectiveBrightness(Layer layer, uint8_t configuredPercent);

// Wire names for the REST API and the device shadow. Both directions live here so
// the mapping is stated once.
const char *patternName(Pattern pattern);
const char *layerName(Layer layer);
bool patternFromName(const char *name, Pattern &out);

}  // namespace LedState
