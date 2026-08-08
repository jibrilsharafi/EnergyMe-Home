// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "led_state.h"

#include <cstring>

namespace LedState {

namespace {

constexpr uint32_t FULL_PERMILLE = 1000;

// Indexed by the enum value, so each name is stated once and both directions read
// from it. Matches the table idiom in issue_logic and shadow_logic.
const char *const PATTERN_NAMES[] = {"off",   "solid",        "blink_slow", "blink_fast",
                                     "pulse", "double_blink", "disco"};
static_assert(sizeof(PATTERN_NAMES) / sizeof(PATTERN_NAMES[0]) == PATTERN_COUNT,
              "Every pattern needs a wire name");

const char *const LAYER_NAMES[] = {"status", "user", "network", "alert", "critical"};
static_assert(sizeof(LAYER_NAMES) / sizeof(LAYER_NAMES[0]) == LAYER_COUNT,
              "Every layer needs a wire name");

uint64_t _elapsed(const Slot &slot, uint64_t nowMs) {
    return nowMs >= slot.setAtMs ? nowMs - slot.setAtMs : 0;
}

// The single place brightness enters the output. factorPermille is the waveform's
// own dimming (only PULSE uses anything but full), folded into the same multiply so
// that brightness cannot be applied twice - which is what made PULSE peak at
// brightness squared before this module existed.
uint8_t _scale(uint8_t value, uint8_t brightnessPercent, uint32_t factorPermille) {
    const uint32_t scaled = (uint32_t)value * (uint32_t)brightnessPercent * factorPermille;
    const uint8_t result = (uint8_t)(scaled / (MAX_BRIGHTNESS_PERCENT * FULL_PERMILLE));

    // Truncation must not silence a channel that should be lit. At a low brightness
    // floor a dim channel divides to 0, which would render an indication completely
    // dark exactly when the floor exists to make it visible.
    if (result == 0 && scaled > 0) { return 1; }
    return result;
}

Rgb _scaleColor(Rgb color, uint8_t brightnessPercent, uint32_t factorPermille) {
    Rgb out;
    out.red = _scale(color.red, brightnessPercent, factorPermille);
    out.green = _scale(color.green, brightnessPercent, factorPermille);
    out.blue = _scale(color.blue, brightnessPercent, factorPermille);
    return out;
}

// Triangle wave over a 2 x half-period cycle, in permille of full.
uint32_t _pulseFactorPermille(uint64_t elapsedMs) {
    const uint64_t cycle = elapsedMs % (2 * PULSE_HALF_MS);
    const uint64_t rising = cycle < PULSE_HALF_MS ? cycle : (2 * PULSE_HALF_MS) - cycle;
    return (uint32_t)((rising * FULL_PERMILLE) / PULSE_HALF_MS);
}

bool _blinkIsOn(uint64_t elapsedMs, uint64_t halfPeriodMs) {
    return ((elapsedMs / halfPeriodMs) % 2) == 0;
}

// Saturated colours only. Free RGB spends most of its range on muddy values that read
// as a broken LED, and can land on near-black; a fixed palette always looks deliberate
// and lets the tests assert exact colours instead of statistics.
constexpr Rgb DISCO_PALETTE[] = {Colors::RED,    Colors::GREEN,  Colors::BLUE,   Colors::YELLOW,
                                 Colors::PURPLE, Colors::CYAN,   Colors::ORANGE, Colors::WHITE};
constexpr uint8_t DISCO_PALETTE_SIZE = sizeof(DISCO_PALETTE) / sizeof(DISCO_PALETTE[0]);
static_assert(DISCO_PALETTE_SIZE > 1 && DISCO_PALETTE_SIZE % 2 == 0,
              "discoColor() splits the palette into two even/odd halves");

// xorshift32 over (seed, stepIndex). The golden-ratio multiply decorrelates adjacent
// steps, so each step's hash stands alone rather than being iterated from the last.
uint32_t _discoHash(uint32_t seed, uint32_t stepIndex) {
    uint32_t h = seed ^ (stepIndex * 0x9E3779B9u);
    h ^= h << 13;
    h ^= h >> 17;
    h ^= h << 5;
    return h;
}

}  // namespace

void set(Table &table, Layer layer, Pattern pattern, Rgb color, uint64_t nowMs, uint64_t durationMs,
         uint32_t seed) {
    Slot &slot = table.slots[(uint8_t)layer];
    slot.occupied = true;
    slot.pattern = pattern;
    slot.color = color;
    slot.setAtMs = nowMs;
    slot.durationMs = durationMs;
    slot.seed = seed;
}

void release(Table &table, Layer layer) {
    table.slots[(uint8_t)layer] = Slot{};
}

void releaseAll(Table &table) {
    for (uint8_t i = 0; i < LAYER_COUNT; i++) { table.slots[i] = Slot{}; }
}

bool expire(Table &table, uint64_t nowMs) {
    bool changed = false;
    for (uint8_t i = 0; i < LAYER_COUNT; i++) {
        Slot &slot = table.slots[i];
        if (!slot.occupied || slot.durationMs == 0) { continue; }
        if (_elapsed(slot, nowMs) >= slot.durationMs) {
            slot = Slot{};
            changed = true;
        }
    }
    return changed;
}

Active resolve(const Table &table, uint64_t nowMs) {
    Active active;

    for (int8_t i = LAYER_COUNT - 1; i >= 0; i--) {
        const Slot &slot = table.slots[i];
        if (!slot.occupied) { continue; }

        active.any = true;
        active.layer = (Layer)i;
        active.pattern = slot.pattern;
        active.elapsedMs = _elapsed(slot, nowMs);

        // Resolved here, not in render(), so that everything reading the active
        // indication - the pins, isLit, GET /api/v1/led - sees the colour that is
        // actually showing rather than the placeholder the caller passed in.
        active.color = slot.pattern == Pattern::DISCO ? discoColor(slot.seed, active.elapsedMs)
                                                      : slot.color;
        active.indefinite = (slot.durationMs == 0);
        active.remainingMs = active.indefinite || slot.durationMs <= active.elapsedMs
                                 ? 0
                                 : slot.durationMs - active.elapsedMs;
        return active;
    }

    return active;
}

Frame step(Table &table, uint64_t nowMs, uint8_t configuredBrightnessPercent) {
    expire(table, nowMs);

    Frame frame;
    frame.active = resolve(table, nowMs);
    if (!frame.active.any) { return frame; }

    frame.isOn = isOn(frame.active.pattern, frame.active.elapsedMs);
    frame.output = render(frame.active.pattern, frame.active.color, frame.active.elapsedMs,
                          effectiveBrightness(frame.active.layer, configuredBrightnessPercent));
    return frame;
}

bool isOn(Pattern pattern, uint64_t elapsedMs) {
    switch (pattern) {
    case Pattern::SOLID:
    case Pattern::DISCO:
        return true;

    case Pattern::BLINK_SLOW:
        return _blinkIsOn(elapsedMs, BLINK_SLOW_HALF_MS);

    case Pattern::BLINK_FAST:
        return _blinkIsOn(elapsedMs, BLINK_FAST_HALF_MS);

    case Pattern::PULSE:
        return _pulseFactorPermille(elapsedMs) > 0;

    case Pattern::DOUBLE_BLINK: {
        const uint64_t cycle = elapsedMs % DOUBLE_BLINK_CYCLE_MS;
        const uint64_t seg = DOUBLE_BLINK_SEGMENT_MS;
        return cycle < seg || (cycle >= 2 * seg && cycle < 3 * seg);
    }

    case Pattern::OFF:
    case Pattern::Count:
        break;
    }
    return false;
}

Rgb render(Pattern pattern, Rgb color, uint64_t elapsedMs, uint8_t brightnessPercent) {
    if (brightnessPercent > MAX_BRIGHTNESS_PERCENT) { brightnessPercent = MAX_BRIGHTNESS_PERCENT; }
    if (!isOn(pattern, elapsedMs)) { return Rgb{}; }

    const uint32_t factor = pattern == Pattern::PULSE ? _pulseFactorPermille(elapsedMs) : FULL_PERMILLE;
    return _scaleColor(color, brightnessPercent, factor);
}

Rgb discoColor(uint32_t seed, uint64_t elapsedMs) {
    const uint32_t stepIndex = (uint32_t)(elapsedMs / DISCO_STEP_MS);

    // The palette splits into two disjoint halves, one for even step indices and one
    // for odd. Consecutive steps always draw from different halves, so they can never
    // land on the same colour - a structural guarantee rather than a probabilistic
    // one. This costs a single hash call regardless of how long disco has been
    // running: no walk, no retained history, no bound on step count needed to keep it
    // cheap (an earlier version walked the palette from step 0 on every call, which
    // made the render path grow with uptime and, at scale, occasionally let a
    // multi-step hash collision slip a repeat past the walk anyway).
    const uint32_t half = _discoHash(seed, stepIndex) % (DISCO_PALETTE_SIZE / 2);
    const uint8_t index = (uint8_t)(2 * half + (stepIndex % 2));
    return DISCO_PALETTE[index];
}

uint8_t effectiveBrightness(Layer layer, uint8_t configuredPercent) {
    if (configuredPercent > MAX_BRIGHTNESS_PERCENT) { configuredPercent = MAX_BRIGHTNESS_PERCENT; }

    const uint8_t floorPercent = LAYER_MIN_BRIGHTNESS_PERCENT[(uint8_t)layer];
    return configuredPercent < floorPercent ? floorPercent : configuredPercent;
}

const char *patternName(Pattern pattern) {
    if ((uint8_t)pattern >= PATTERN_COUNT) { return PATTERN_NAMES[(uint8_t)Pattern::OFF]; }
    return PATTERN_NAMES[(uint8_t)pattern];
}

const char *layerName(Layer layer) {
    if ((uint8_t)layer >= LAYER_COUNT) { return LAYER_NAMES[(uint8_t)Layer::STATUS]; }
    return LAYER_NAMES[(uint8_t)layer];
}

bool patternFromName(const char *name, Pattern &out) {
    if (name == nullptr) { return false; }

    for (uint8_t i = 0; i < PATTERN_COUNT; i++) {
        if (strcmp(name, PATTERN_NAMES[i]) == 0) {
            out = (Pattern)i;
            return true;
        }
    }
    return false;
}

}  // namespace LedState
