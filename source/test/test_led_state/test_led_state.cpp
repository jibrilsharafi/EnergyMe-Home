// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for the LED layer table and renderer. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>

#include "led_state.h"

using namespace LedState;

void setUp(void) {}
void tearDown(void) {}

namespace {

const Rgb RED{255, 0, 0};
const Rgb GREEN{0, 255, 0};
const Rgb BLUE{0, 0, 255};
const Rgb MAGENTA{255, 0, 255};
const Rgb DARK{0, 0, 0};

constexpr uint8_t FULL = 100;

// The renderer's sampling period in the firmware. Waveform assertions use it so a
// pattern that only looks right at exact boundaries cannot pass.
constexpr uint64_t TICK_MS = 25;

// Mirrors exactly what led.cpp's render task does each tick: expire, then resolve,
// then render. Tests go through this so the real call sequence is the tested one.
Rgb renderAt(Table &table, uint64_t nowMs, uint8_t brightness = FULL) {
    expire(table, nowMs);
    const Active active = resolve(table, nowMs);
    if (!active.any) { return Rgb{}; }
    return render(active.pattern, active.color, active.elapsedMs,
                  effectiveBrightness(active.layer, brightness));
}

void assertColor(Rgb expected, Rgb actual) {
    TEST_ASSERT_EQUAL_UINT8(expected.red, actual.red);
    TEST_ASSERT_EQUAL_UINT8(expected.green, actual.green);
    TEST_ASSERT_EQUAL_UINT8(expected.blue, actual.blue);
}

bool isLit(Rgb c) { return c.red != 0 || c.green != 0 || c.blue != 0; }

}  // namespace

// --- Layer precedence -------------------------------------------------------------

void test_empty_table_renders_dark(void) {
    Table table;
    TEST_ASSERT_FALSE(resolve(table, 0).any);
    assertColor(DARK, renderAt(table, 0));
}

void test_highest_occupied_layer_wins(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);
    set(table, Layer::NETWORK, Pattern::PULSE, BLUE, 0, 0);

    const Active active = resolve(table, 0);
    TEST_ASSERT_TRUE(active.any);
    TEST_ASSERT_EQUAL(Layer::NETWORK, active.layer);
    TEST_ASSERT_EQUAL(Pattern::PULSE, active.pattern);
    assertColor(BLUE, active.color);
}

void test_layer_order_is_status_user_network_alert_critical(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);
    TEST_ASSERT_EQUAL(Layer::STATUS, resolve(table, 0).layer);

    // USER above STATUS, not below it. The firmware occupies STATUS at boot and
    // never releases it, so a user colour underneath could never be seen.
    set(table, Layer::USER, Pattern::SOLID, MAGENTA, 0, 0);
    TEST_ASSERT_EQUAL(Layer::USER, resolve(table, 0).layer);

    set(table, Layer::NETWORK, Pattern::SOLID, BLUE, 0, 0);
    TEST_ASSERT_EQUAL(Layer::NETWORK, resolve(table, 0).layer);

    set(table, Layer::ALERT, Pattern::SOLID, RED, 0, 0);
    TEST_ASSERT_EQUAL(Layer::ALERT, resolve(table, 0).layer);

    set(table, Layer::CRITICAL, Pattern::SOLID, RED, 0, 0);
    TEST_ASSERT_EQUAL(Layer::CRITICAL, resolve(table, 0).layer);
}

void test_writing_a_lower_layer_does_not_disturb_the_visible_one(void) {
    Table table;
    set(table, Layer::CRITICAL, Pattern::SOLID, RED, 0, 0);
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);

    const Active active = resolve(table, 0);
    TEST_ASSERT_EQUAL(Layer::CRITICAL, active.layer);
    assertColor(RED, active.color);

    // ...and the lower layer still took the write.
    release(table, Layer::CRITICAL);
    assertColor(GREEN, resolve(table, 0).color);
}

// This is the regression test for the reported bug. Under the old queue, an
// indefinite critical indication meant every lower-priority command lost
// arbitration, was re-queued, and spun until the ten-entry queue filled and started
// dropping - so the LED stopped tracking device state. Here the writes always land.
void test_lower_layer_writes_are_never_dropped_under_an_indefinite_critical(void) {
    Table table;
    set(table, Layer::CRITICAL, Pattern::BLINK_FAST, RED, 0, 0);

    for (uint64_t i = 0; i < 100; i++) {
        set(table, Layer::STATUS, Pattern::SOLID, i % 2 == 0 ? GREEN : BLUE, i, 0);
        TEST_ASSERT_EQUAL(Layer::CRITICAL, resolve(table, i).layer);
    }

    // The 100th write (i == 99, odd) is what the status layer holds.
    release(table, Layer::CRITICAL);
    const Active active = resolve(table, 100);
    TEST_ASSERT_EQUAL(Layer::STATUS, active.layer);
    assertColor(BLUE, active.color);
}

void test_same_layer_write_replaces_rather_than_queues(void) {
    Table table;
    set(table, Layer::ALERT, Pattern::SOLID, RED, 0, 0);
    set(table, Layer::ALERT, Pattern::SOLID, GREEN, 10, 0);

    assertColor(GREEN, resolve(table, 10).color);

    // The replaced indication must not reappear later.
    for (uint64_t t = 10; t < 10000; t += 500) {
        assertColor(GREEN, resolve(table, t).color);
    }
}

void test_set_restarts_the_waveform_phase(void) {
    Table table;
    set(table, Layer::ALERT, Pattern::BLINK_SLOW, RED, 0, 0);
    TEST_ASSERT_FALSE(isLit(renderAt(table, 1500)));  // in the off half

    set(table, Layer::ALERT, Pattern::BLINK_SLOW, RED, 1500, 0);
    TEST_ASSERT_TRUE(isLit(renderAt(table, 1500)));  // phase restarted: on again
}

// --- Release and expiry -----------------------------------------------------------

void test_release_reveals_the_layer_below(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);
    set(table, Layer::NETWORK, Pattern::PULSE, BLUE, 0, 0);

    release(table, Layer::NETWORK);

    const Active active = resolve(table, 0);
    TEST_ASSERT_EQUAL(Layer::STATUS, active.layer);
    TEST_ASSERT_EQUAL(Pattern::SOLID, active.pattern);
    assertColor(GREEN, active.color);
}

void test_releasing_a_free_layer_is_a_no_op(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);

    release(table, Layer::ALERT);
    release(table, Layer::CRITICAL);
    release(table, Layer::USER);

    TEST_ASSERT_EQUAL(Layer::STATUS, resolve(table, 0).layer);
}

void test_release_all_renders_dark(void) {
    Table table;
    set(table, Layer::USER, Pattern::SOLID, MAGENTA, 0, 0);
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);
    set(table, Layer::CRITICAL, Pattern::SOLID, RED, 0, 0);

    releaseAll(table);

    TEST_ASSERT_FALSE(resolve(table, 0).any);
    assertColor(DARK, renderAt(table, 0));
}

void test_duration_expiry_releases_the_layer(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);
    set(table, Layer::ALERT, Pattern::BLINK_FAST, GREEN, 0, 2000);

    TEST_ASSERT_FALSE(expire(table, 1999));
    TEST_ASSERT_EQUAL(Layer::ALERT, resolve(table, 1999).layer);

    TEST_ASSERT_TRUE(expire(table, 2000));
    const Active active = resolve(table, 2000);
    TEST_ASSERT_EQUAL(Layer::STATUS, active.layer);
    TEST_ASSERT_EQUAL(Pattern::SOLID, active.pattern);
}

// The old queue restarted a re-queued command's duration whenever it was finally
// accepted, so a 2 s indication could surface minutes later and run in full.
void test_duration_is_measured_from_set_time_even_while_masked(void) {
    Table table;
    set(table, Layer::ALERT, Pattern::BLINK_FAST, GREEN, 0, 2000);
    set(table, Layer::CRITICAL, Pattern::SOLID, RED, 0, 0);

    // Masked for its whole lifetime.
    TEST_ASSERT_TRUE(expire(table, 5000));
    release(table, Layer::CRITICAL);

    TEST_ASSERT_FALSE(resolve(table, 5000).any);
}

void test_shortest_duration_expires_on_its_own_tick(void) {
    Table table;
    set(table, Layer::USER, Pattern::SOLID, MAGENTA, 1000, 1);

    TEST_ASSERT_FALSE(expire(table, 1000));
    TEST_ASSERT_TRUE(expire(table, 1001));
    TEST_ASSERT_FALSE(resolve(table, 1001).any);
}

// expire() has to sweep every slot, not stop at the first one it frees.
void test_several_layers_expire_on_the_same_tick(void) {
    Table table;
    set(table, Layer::USER, Pattern::SOLID, MAGENTA, 0, 1000);
    set(table, Layer::NETWORK, Pattern::SOLID, BLUE, 0, 1000);
    set(table, Layer::ALERT, Pattern::SOLID, RED, 0, 1000);
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);

    TEST_ASSERT_TRUE(expire(table, 1000));

    TEST_ASSERT_EQUAL(Layer::STATUS, resolve(table, 1000).layer);
    TEST_ASSERT_FALSE(expire(table, 1000));
}

// led.cpp samples millis64() before taking the mutex, so a concurrent set() can
// land with setAtMs just past the timestamp the render pass is using.
void test_a_set_time_in_the_future_clamps_to_zero_elapsed(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::BLINK_SLOW, GREEN, 1000, 5000);

    const Active active = resolve(table, 999);
    TEST_ASSERT_EQUAL_UINT64(0, active.elapsedMs);
    TEST_ASSERT_EQUAL_UINT64(5000, active.remainingMs);
    TEST_ASSERT_FALSE(expire(table, 999));
}

void test_waveforms_hold_far_beyond_the_first_cycle(void) {
    // A common multiple of every pattern period (500, 1200, 2000), so each one is
    // back at phase zero and must look exactly as it did at elapsed 0.
    const uint64_t late = 6000ULL * 200000ULL;  // ~13.9 days of uptime

    TEST_ASSERT_TRUE(isLit(render(Pattern::BLINK_SLOW, RED, late, FULL)));
    TEST_ASSERT_FALSE(isLit(render(Pattern::BLINK_SLOW, RED, late + 1000, FULL)));
    TEST_ASSERT_TRUE(isLit(render(Pattern::BLINK_FAST, RED, late, FULL)));
    TEST_ASSERT_FALSE(isLit(render(Pattern::BLINK_FAST, RED, late + 250, FULL)));
    TEST_ASSERT_TRUE(isLit(render(Pattern::DOUBLE_BLINK, RED, late, FULL)));
    TEST_ASSERT_FALSE(isLit(render(Pattern::DOUBLE_BLINK, RED, late + 100, FULL)));
    assertColor(DARK, render(Pattern::PULSE, RED, late, FULL));
    assertColor(RED, render(Pattern::PULSE, RED, late + 1000, FULL));
}

void test_indefinite_indications_never_expire(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);

    TEST_ASSERT_FALSE(expire(table, 0xFFFFFFFFULL));
    TEST_ASSERT_TRUE(resolve(table, 0xFFFFFFFFULL).any);
}

void test_remaining_and_indefinite_are_reported(void) {
    Table table;
    set(table, Layer::USER, Pattern::SOLID, MAGENTA, 1000, 5000);

    Active active = resolve(table, 2000);
    TEST_ASSERT_FALSE(active.indefinite);
    TEST_ASSERT_EQUAL_UINT64(4000, active.remainingMs);
    TEST_ASSERT_EQUAL_UINT64(1000, active.elapsedMs);

    set(table, Layer::USER, Pattern::SOLID, MAGENTA, 1000, 0);
    active = resolve(table, 2000);
    TEST_ASSERT_TRUE(active.indefinite);
    TEST_ASSERT_EQUAL_UINT64(0, active.remainingMs);
}

// --- Waveforms --------------------------------------------------------------------

void test_solid_is_always_on(void) {
    for (uint64_t t = 0; t < 5000; t += TICK_MS) {
        assertColor(RED, render(Pattern::SOLID, RED, t, FULL));
    }
}

void test_off_is_always_dark(void) {
    for (uint64_t t = 0; t < 5000; t += TICK_MS) {
        assertColor(DARK, render(Pattern::OFF, RED, t, FULL));
    }
}

void test_blink_slow_half_period(void) {
    TEST_ASSERT_TRUE(isLit(render(Pattern::BLINK_SLOW, RED, 0, FULL)));
    TEST_ASSERT_TRUE(isLit(render(Pattern::BLINK_SLOW, RED, 975, FULL)));
    TEST_ASSERT_FALSE(isLit(render(Pattern::BLINK_SLOW, RED, 1000, FULL)));
    TEST_ASSERT_FALSE(isLit(render(Pattern::BLINK_SLOW, RED, 1975, FULL)));
    TEST_ASSERT_TRUE(isLit(render(Pattern::BLINK_SLOW, RED, 2000, FULL)));
}

void test_blink_fast_half_period(void) {
    TEST_ASSERT_TRUE(isLit(render(Pattern::BLINK_FAST, RED, 0, FULL)));
    TEST_ASSERT_TRUE(isLit(render(Pattern::BLINK_FAST, RED, 225, FULL)));
    TEST_ASSERT_FALSE(isLit(render(Pattern::BLINK_FAST, RED, 250, FULL)));
    TEST_ASSERT_FALSE(isLit(render(Pattern::BLINK_FAST, RED, 475, FULL)));
    TEST_ASSERT_TRUE(isLit(render(Pattern::BLINK_FAST, RED, 500, FULL)));
}

// A 25 ms tick must land inside every segment of the shortest pattern. At the old
// ~100 ms effective period, a 100 ms double-blink segment could be stepped over
// entirely and the second blink simply never appeared.
void test_double_blink_segments_are_all_sampled(void) {
    bool seen[LED_STATE_DOUBLE_BLINK_CYCLE_MS / LED_STATE_DOUBLE_BLINK_SEGMENT_MS] = {false};

    for (uint64_t t = 0; t < LED_STATE_DOUBLE_BLINK_CYCLE_MS; t += TICK_MS) {
        if (isLit(render(Pattern::DOUBLE_BLINK, RED, t, FULL))) {
            seen[t / LED_STATE_DOUBLE_BLINK_SEGMENT_MS] = true;
        }
    }

    TEST_ASSERT_TRUE(seen[0]);   // first blink
    TEST_ASSERT_FALSE(seen[1]);  // gap
    TEST_ASSERT_TRUE(seen[2]);   // second blink
    for (size_t i = 3; i < sizeof(seen) / sizeof(seen[0]); i++) {
        TEST_ASSERT_FALSE(seen[i]);  // long pause
    }
}

void test_double_blink_boundaries(void) {
    TEST_ASSERT_TRUE(isLit(render(Pattern::DOUBLE_BLINK, RED, 0, FULL)));
    TEST_ASSERT_TRUE(isLit(render(Pattern::DOUBLE_BLINK, RED, 75, FULL)));
    TEST_ASSERT_FALSE(isLit(render(Pattern::DOUBLE_BLINK, RED, 100, FULL)));
    TEST_ASSERT_TRUE(isLit(render(Pattern::DOUBLE_BLINK, RED, 200, FULL)));
    TEST_ASSERT_FALSE(isLit(render(Pattern::DOUBLE_BLINK, RED, 300, FULL)));
    TEST_ASSERT_FALSE(isLit(render(Pattern::DOUBLE_BLINK, RED, 1100, FULL)));
    TEST_ASSERT_TRUE(isLit(render(Pattern::DOUBLE_BLINK, RED, 1200, FULL)));  // cycle repeats
}

void test_pulse_is_a_triangle_between_dark_and_full(void) {
    assertColor(DARK, render(Pattern::PULSE, RED, 0, FULL));
    assertColor(RED, render(Pattern::PULSE, RED, 1000, FULL));
    assertColor(DARK, render(Pattern::PULSE, RED, 2000, FULL));

    TEST_ASSERT_EQUAL_UINT8(127, render(Pattern::PULSE, RED, 500, FULL).red);
    TEST_ASSERT_EQUAL_UINT8(127, render(Pattern::PULSE, RED, 1500, FULL).red);

    // Monotonic on the way up, so the fade cannot stutter.
    uint8_t previous = 0;
    for (uint64_t t = TICK_MS; t <= 1000; t += TICK_MS) {
        const uint8_t current = render(Pattern::PULSE, RED, t, FULL).red;
        TEST_ASSERT_GREATER_THAN_UINT8(previous, current);
        previous = current;
    }
}

// --- Brightness -------------------------------------------------------------------

void test_brightness_scales_output(void) {
    assertColor(Rgb{127, 0, 0}, render(Pattern::SOLID, RED, 0, 50));
    assertColor(Rgb{255, 0, 0}, render(Pattern::SOLID, RED, 0, 100));
    assertColor(DARK, render(Pattern::SOLID, RED, 0, 0));
}

void test_brightness_above_max_is_clamped(void) {
    assertColor(Rgb{255, 0, 0}, render(Pattern::SOLID, RED, 0, 200));
}

// PULSE used to run its colour through the brightness multiply once in the pattern
// step and again in the pin write, so at 75% it peaked at 0.75^2 = 56%.
void test_pulse_peak_equals_solid_at_the_same_brightness(void) {
    for (uint8_t brightness = 0; brightness <= 100; brightness += 5) {
        assertColor(render(Pattern::SOLID, MAGENTA, 0, brightness),
                    render(Pattern::PULSE, MAGENTA, 1000, brightness));
    }
}

void test_attention_layers_floor_brightness(void) {
    TEST_ASSERT_EQUAL_UINT8(LED_STATE_CRITICAL_MIN_BRIGHTNESS_PERCENT,
                            effectiveBrightness(Layer::CRITICAL, 0));
    TEST_ASSERT_EQUAL_UINT8(LED_STATE_ALERT_MIN_BRIGHTNESS_PERCENT,
                            effectiveBrightness(Layer::ALERT, 0));

    // The floor only lifts, never dims.
    TEST_ASSERT_EQUAL_UINT8(75, effectiveBrightness(Layer::CRITICAL, 75));
    TEST_ASSERT_EQUAL_UINT8(75, effectiveBrightness(Layer::ALERT, 75));
    TEST_ASSERT_EQUAL_UINT8(5, effectiveBrightness(Layer::ALERT, 5));

    // Ordering is a requirement, not an accident of the two macro values.
    TEST_ASSERT_GREATER_THAN_UINT8(effectiveBrightness(Layer::ALERT, 0),
                                   effectiveBrightness(Layer::CRITICAL, 0));
}

// At the 1% floor every channel below 100 divides to zero, so a dim indication
// would render completely dark exactly when the floor exists to make it visible.
void test_a_floored_dim_colour_still_lights(void) {
    const Rgb dimRed{50, 0, 0};

    TEST_ASSERT_TRUE(isLit(render(Pattern::SOLID, dimRed, 0,
                                  effectiveBrightness(Layer::ALERT, 0))));
    TEST_ASSERT_TRUE(isLit(render(Pattern::SOLID, Rgb{1, 0, 0}, 0,
                                  effectiveBrightness(Layer::ALERT, 0))));

    // Still genuinely dark when the colour or the brightness really is zero.
    assertColor(DARK, render(Pattern::SOLID, DARK, 0, 100));
    assertColor(DARK, render(Pattern::SOLID, dimRed, 0, 0));
}

void test_ambient_layers_honour_zero_brightness(void) {
    TEST_ASSERT_EQUAL_UINT8(0, effectiveBrightness(Layer::USER, 0));
    TEST_ASSERT_EQUAL_UINT8(0, effectiveBrightness(Layer::STATUS, 0));
    TEST_ASSERT_EQUAL_UINT8(0, effectiveBrightness(Layer::NETWORK, 0));
}

// The button ladder runs on ALERT, and its confirmation colours are what tell the
// user which threshold - factory reset included - they are about to trigger. The
// call site used to buy that visibility with setBrightness(max(get(), 1)), which
// persisted.
void test_button_feedback_is_visible_at_zero_brightness(void) {
    Table table;
    set(table, Layer::ALERT, Pattern::SOLID, Rgb{255, 255, 255}, 0, 0);

    TEST_ASSERT_TRUE(isLit(renderAt(table, 0, 0)));

    release(table, Layer::ALERT);
    assertColor(DARK, renderAt(table, 0, 0));
}

void test_critical_stays_visible_at_zero_brightness(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);
    set(table, Layer::CRITICAL, Pattern::SOLID, RED, 0, 0);

    TEST_ASSERT_TRUE(isLit(renderAt(table, 0, 0)));

    release(table, Layer::CRITICAL);
    assertColor(DARK, renderAt(table, 0, 0));  // and the floor does not leak downwards
}

// --- Wire names -------------------------------------------------------------------

void test_pattern_names_round_trip(void) {
    const Pattern all[] = {Pattern::OFF,        Pattern::SOLID, Pattern::BLINK_SLOW,
                           Pattern::BLINK_FAST, Pattern::PULSE, Pattern::DOUBLE_BLINK};

    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        Pattern parsed = Pattern::SOLID;
        TEST_ASSERT_TRUE(patternFromName(patternName(all[i]), parsed));
        TEST_ASSERT_EQUAL(all[i], parsed);
    }
}

void test_unknown_pattern_name_is_rejected(void) {
    Pattern parsed = Pattern::OFF;
    TEST_ASSERT_FALSE(patternFromName("rainbow", parsed));
    TEST_ASSERT_FALSE(patternFromName("", parsed));
    TEST_ASSERT_FALSE(patternFromName(nullptr, parsed));
    TEST_ASSERT_FALSE(patternFromName("SOLID", parsed));  // case sensitive
    TEST_ASSERT_EQUAL(Pattern::OFF, parsed);              // untouched on failure
}

void test_layer_names(void) {
    TEST_ASSERT_EQUAL_STRING("user", layerName(Layer::USER));
    TEST_ASSERT_EQUAL_STRING("status", layerName(Layer::STATUS));
    TEST_ASSERT_EQUAL_STRING("network", layerName(Layer::NETWORK));
    TEST_ASSERT_EQUAL_STRING("alert", layerName(Layer::ALERT));
    TEST_ASSERT_EQUAL_STRING("critical", layerName(Layer::CRITICAL));
}

// --- Sequences the firmware actually produces -------------------------------------

// The WiFi task used to re-assert green by hand on reconnect because clearing the
// network layer meant "turn the LED off". See customwifi.cpp:587's "Hack:" comment.
void test_wifi_reconnect_falls_back_to_status_without_re_asserting(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);  // healthy, set once at boot

    set(table, Layer::NETWORK, Pattern::PULSE, BLUE, 1000, 0);  // link lost
    TEST_ASSERT_EQUAL(Layer::NETWORK, resolve(table, 1000).layer);

    release(table, Layer::NETWORK);  // link back
    const Active active = resolve(table, 2000);
    TEST_ASSERT_EQUAL(Layer::STATUS, active.layer);
    assertColor(GREEN, active.color);
}

// The user story from issue #105, against the layers a real healthy device holds.
// STATUS is occupied for the whole uptime (main.cpp sets green at the end of setup
// and nothing ever releases it), so this is the sequence that decides whether the
// API does anything at all.
void test_user_colour_shows_on_a_healthy_device(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);  // healthy, never released

    set(table, Layer::USER, Pattern::SOLID, MAGENTA, 100, 0);
    const Active active = resolve(table, 100);
    TEST_ASSERT_EQUAL(Layer::USER, active.layer);
    assertColor(MAGENTA, active.color);

    release(table, Layer::USER);
    assertColor(GREEN, resolve(table, 200).color);
}

// A user colour must never hide a fault, and must come back once the fault clears.
void test_user_colour_is_overridden_by_events_and_returns(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);
    set(table, Layer::USER, Pattern::SOLID, MAGENTA, 0, 0);
    assertColor(MAGENTA, resolve(table, 0).color);

    set(table, Layer::NETWORK, Pattern::PULSE, BLUE, 100, 0);  // link lost
    TEST_ASSERT_EQUAL(Layer::NETWORK, resolve(table, 100).layer);

    set(table, Layer::CRITICAL, Pattern::SOLID, RED, 200, 0);  // safe mode
    assertColor(RED, resolve(table, 200).color);

    release(table, Layer::CRITICAL);
    release(table, Layer::NETWORK);
    assertColor(MAGENTA, resolve(table, 300).color);
}

// "Turn the LED off" from an automation must suppress the ambient green, which is
// why Pattern::OFF occupies the user layer instead of releasing it.
void test_user_off_suppresses_the_ambient_status(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);
    set(table, Layer::USER, Pattern::OFF, DARK, 0, 0);

    TEST_ASSERT_EQUAL(Layer::USER, resolve(table, 0).layer);
    assertColor(DARK, renderAt(table, 0));

    // ...but an event still gets through.
    set(table, Layer::CRITICAL, Pattern::SOLID, RED, 100, 0);
    TEST_ASSERT_TRUE(isLit(renderAt(table, 100)));
}

void test_button_press_release_reveals_underlying_status(void) {
    Table table;
    set(table, Layer::STATUS, Pattern::SOLID, GREEN, 0, 0);

    set(table, Layer::ALERT, Pattern::SOLID, Rgb{255, 255, 255}, 100, 0);  // held
    assertColor(Rgb{255, 255, 255}, resolve(table, 100).color);

    release(table, Layer::ALERT);  // released
    assertColor(GREEN, resolve(table, 200).color);
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_empty_table_renders_dark);
    RUN_TEST(test_highest_occupied_layer_wins);
    RUN_TEST(test_layer_order_is_status_user_network_alert_critical);
    RUN_TEST(test_writing_a_lower_layer_does_not_disturb_the_visible_one);
    RUN_TEST(test_lower_layer_writes_are_never_dropped_under_an_indefinite_critical);
    RUN_TEST(test_same_layer_write_replaces_rather_than_queues);
    RUN_TEST(test_set_restarts_the_waveform_phase);

    RUN_TEST(test_release_reveals_the_layer_below);
    RUN_TEST(test_releasing_a_free_layer_is_a_no_op);
    RUN_TEST(test_release_all_renders_dark);
    RUN_TEST(test_duration_expiry_releases_the_layer);
    RUN_TEST(test_duration_is_measured_from_set_time_even_while_masked);
    RUN_TEST(test_shortest_duration_expires_on_its_own_tick);
    RUN_TEST(test_several_layers_expire_on_the_same_tick);
    RUN_TEST(test_a_set_time_in_the_future_clamps_to_zero_elapsed);
    RUN_TEST(test_waveforms_hold_far_beyond_the_first_cycle);
    RUN_TEST(test_indefinite_indications_never_expire);
    RUN_TEST(test_remaining_and_indefinite_are_reported);

    RUN_TEST(test_solid_is_always_on);
    RUN_TEST(test_off_is_always_dark);
    RUN_TEST(test_blink_slow_half_period);
    RUN_TEST(test_blink_fast_half_period);
    RUN_TEST(test_double_blink_segments_are_all_sampled);
    RUN_TEST(test_double_blink_boundaries);
    RUN_TEST(test_pulse_is_a_triangle_between_dark_and_full);

    RUN_TEST(test_brightness_scales_output);
    RUN_TEST(test_brightness_above_max_is_clamped);
    RUN_TEST(test_pulse_peak_equals_solid_at_the_same_brightness);
    RUN_TEST(test_attention_layers_floor_brightness);
    RUN_TEST(test_a_floored_dim_colour_still_lights);
    RUN_TEST(test_ambient_layers_honour_zero_brightness);
    RUN_TEST(test_critical_stays_visible_at_zero_brightness);
    RUN_TEST(test_button_feedback_is_visible_at_zero_brightness);

    RUN_TEST(test_pattern_names_round_trip);
    RUN_TEST(test_unknown_pattern_name_is_rejected);
    RUN_TEST(test_layer_names);

    RUN_TEST(test_wifi_reconnect_falls_back_to_status_without_re_asserting);
    RUN_TEST(test_user_colour_shows_on_a_healthy_device);
    RUN_TEST(test_user_colour_is_overridden_by_events_and_returns);
    RUN_TEST(test_user_off_suppresses_the_ambient_status);
    RUN_TEST(test_button_press_release_reveals_underlying_status);

    return UNITY_END();
}
