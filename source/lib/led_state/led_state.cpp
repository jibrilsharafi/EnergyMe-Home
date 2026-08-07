// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "led_state.h"

#include <cstring>

namespace LedState {

namespace {

constexpr uint32_t FULL_PERMILLE = 1000;

uint64_t _elapsed(const Slot &slot, uint64_t nowMs) {
    return nowMs >= slot.setAtMs ? nowMs - slot.setAtMs : 0;
}

// The single place brightness enters the output. factorPermille is the waveform's
// own dimming (only PULSE uses anything but full), folded into the same multiply so
// that brightness cannot be applied twice - which is what made PULSE peak at
// brightness squared before this module existed.
uint8_t _scale(uint8_t value, uint8_t brightnessPercent, uint32_t factorPermille) {
    if (brightnessPercent > LED_STATE_MAX_BRIGHTNESS_PERCENT) {
        brightnessPercent = LED_STATE_MAX_BRIGHTNESS_PERCENT;
    }
    if (factorPermille > FULL_PERMILLE) { factorPermille = FULL_PERMILLE; }

    const uint32_t scaled = (uint32_t)value * (uint32_t)brightnessPercent * factorPermille;
    return (uint8_t)(scaled / (LED_STATE_MAX_BRIGHTNESS_PERCENT * FULL_PERMILLE));
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
    const uint64_t cycle = elapsedMs % (2 * LED_STATE_PULSE_HALF_MS);
    const uint64_t rising = cycle < LED_STATE_PULSE_HALF_MS
                                ? cycle
                                : (2 * LED_STATE_PULSE_HALF_MS) - cycle;
    return (uint32_t)((rising * FULL_PERMILLE) / LED_STATE_PULSE_HALF_MS);
}

bool _blinkIsOn(uint64_t elapsedMs, uint64_t halfPeriodMs) {
    return ((elapsedMs / halfPeriodMs) % 2) == 0;
}

bool _doubleBlinkIsOn(uint64_t elapsedMs) {
    const uint64_t cycle = elapsedMs % LED_STATE_DOUBLE_BLINK_CYCLE_MS;
    const uint64_t seg = LED_STATE_DOUBLE_BLINK_SEGMENT_MS;
    return cycle < seg || (cycle >= 2 * seg && cycle < 3 * seg);
}

}  // namespace

void set(Table &table, Layer layer, Pattern pattern, Rgb color, uint64_t nowMs, uint64_t durationMs) {
    Slot &slot = table.slots[(uint8_t)layer];
    slot.occupied = true;
    slot.pattern = pattern;
    slot.color = color;
    slot.setAtMs = nowMs;
    slot.durationMs = durationMs;
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
        active.color = slot.color;
        active.elapsedMs = _elapsed(slot, nowMs);
        active.indefinite = (slot.durationMs == 0);
        active.remainingMs = active.indefinite || slot.durationMs <= active.elapsedMs
                                 ? 0
                                 : slot.durationMs - active.elapsedMs;
        return active;
    }

    return active;
}

Rgb render(Pattern pattern, Rgb color, uint64_t elapsedMs, uint8_t brightnessPercent) {
    switch (pattern) {
    case Pattern::SOLID:
        return _scaleColor(color, brightnessPercent, FULL_PERMILLE);

    case Pattern::BLINK_SLOW:
        return _blinkIsOn(elapsedMs, LED_STATE_BLINK_SLOW_HALF_MS)
                   ? _scaleColor(color, brightnessPercent, FULL_PERMILLE)
                   : Rgb{};

    case Pattern::BLINK_FAST:
        return _blinkIsOn(elapsedMs, LED_STATE_BLINK_FAST_HALF_MS)
                   ? _scaleColor(color, brightnessPercent, FULL_PERMILLE)
                   : Rgb{};

    case Pattern::PULSE:
        return _scaleColor(color, brightnessPercent, _pulseFactorPermille(elapsedMs));

    case Pattern::DOUBLE_BLINK:
        return _doubleBlinkIsOn(elapsedMs)
                   ? _scaleColor(color, brightnessPercent, FULL_PERMILLE)
                   : Rgb{};

    case Pattern::OFF:
        break;
    }

    return Rgb{};
}

uint8_t effectiveBrightness(Layer layer, uint8_t configuredPercent) {
    if (configuredPercent > LED_STATE_MAX_BRIGHTNESS_PERCENT) {
        configuredPercent = LED_STATE_MAX_BRIGHTNESS_PERCENT;
    }
    if (layer == Layer::CRITICAL && configuredPercent < LED_STATE_CRITICAL_MIN_BRIGHTNESS_PERCENT) {
        return LED_STATE_CRITICAL_MIN_BRIGHTNESS_PERCENT;
    }
    return configuredPercent;
}

const char *patternName(Pattern pattern) {
    switch (pattern) {
    case Pattern::SOLID: return "solid";
    case Pattern::BLINK_SLOW: return "blink_slow";
    case Pattern::BLINK_FAST: return "blink_fast";
    case Pattern::PULSE: return "pulse";
    case Pattern::DOUBLE_BLINK: return "double_blink";
    case Pattern::OFF: break;
    }
    return "off";
}

const char *layerName(Layer layer) {
    switch (layer) {
    case Layer::STATUS: return "status";
    case Layer::NETWORK: return "network";
    case Layer::ALERT: return "alert";
    case Layer::CRITICAL: return "critical";
    case Layer::USER: break;
    }
    return "user";
}

bool patternFromName(const char *name, Pattern &out) {
    if (name == nullptr) { return false; }

    static const Pattern ALL[] = {Pattern::OFF,   Pattern::SOLID, Pattern::BLINK_SLOW,
                                  Pattern::BLINK_FAST, Pattern::PULSE, Pattern::DOUBLE_BLINK};

    for (size_t i = 0; i < sizeof(ALL) / sizeof(ALL[0]); i++) {
        if (strcmp(name, patternName(ALL[i])) == 0) {
            out = ALL[i];
            return true;
        }
    }
    return false;
}

}  // namespace LedState
