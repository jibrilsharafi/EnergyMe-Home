// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for the default-password lockdown allowlist. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "web_auth_gate.h"

using namespace WebAuthGate;

void setUp(void) {}
void tearDown(void) {}

namespace {

// Locked down, request arriving on the LAN (the STA netif).
Action locked(const char *url) { return evaluate(true, false, url); }
// Locked down, request arriving on the device's own SoftAP.
Action lockedOnAp(const char *url) { return evaluate(true, true, url); }
Action unlocked(const char *url) { return evaluate(false, false, url); }

}  // namespace

// --- The not-locked-down short circuit -------------------------------------------
//
// This is the test that matters most. Every device in the field that has ever changed
// its password takes this path for every request; a regression here bricks the web UI
// on hardware that was never in scope for this feature.

void test_a_changed_password_allows_everything(void) {
    TEST_ASSERT_EQUAL(Action::ALLOW, unlocked("/api/v1/system/info"));
    TEST_ASSERT_EQUAL(Action::ALLOW, unlocked("/api/v1/ota/upload"));
    TEST_ASSERT_EQUAL(Action::ALLOW, unlocked("/api/v1/system/factory-reset"));
    TEST_ASSERT_EQUAL(Action::ALLOW, unlocked("/api/v1/auth/reset-password"));
    TEST_ASSERT_EQUAL(Action::ALLOW, unlocked("/configuration"));
    TEST_ASSERT_EQUAL(Action::ALLOW, unlocked("/anything/at/all"));
}

void test_a_changed_password_allows_even_a_null_url(void) {
    TEST_ASSERT_EQUAL(Action::ALLOW, unlocked(nullptr));
    TEST_ASSERT_EQUAL(Action::ALLOW, unlocked(""));
}

// --- The allowlist ----------------------------------------------------------------

void test_the_way_out_is_open(void) {
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/api/v1/auth/status"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/api/v1/auth/change-password"));
}

void test_the_gate_page_and_its_assets_are_open(void) {
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/favicon.svg"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/css/styles.css"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/css/forms.css"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/js/api-client.js"));
}

// The device self-probes this route from 127.0.0.1 and five failures walk it into a
// restart loop that ends in a firmware rollback and an NVS wipe. It is exempt
// structurally too (skipServerMiddlewares), so this is the second of two locks.
void test_the_liveness_probe_is_never_blocked(void) {
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/api/v1/health"));
}

// --- Default-deny for the API ------------------------------------------------------

void test_ordinary_api_routes_are_denied(void) {
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/system/info"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/ade7953/meter-values"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/logs"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/backup/configuration"));
}

// The three that matter: firmware upload and factory reset are what an attacker who
// knows the published default would actually reach for, and reset-password is the one
// route whose only effect from inside the locked state is to preserve it.
void test_the_dangerous_routes_are_denied(void) {
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/ota/upload"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/ota/rollback"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/system/factory-reset"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/auth/reset-password"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/restore/filesystem"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/debug/nvs/entry"));
}

// A route nobody has written yet must be refused without anyone remembering to refuse
// it. This is the whole reason the rule is default-deny rather than a blocklist.
void test_an_unknown_api_route_is_denied(void) {
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/not/invented/yet"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v2/system/info"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/"));
}

// --- Page routes -------------------------------------------------------------------

void test_page_routes_go_to_the_gate(void) {
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/configuration"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/calibration"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/channel"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/info"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/integrations"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/log"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/update"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/waveform"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/ade7953-tester"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/swagger-ui"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/swagger.yaml"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/wifi-setup.html"));
}

void test_an_unknown_page_goes_to_the_gate(void) {
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/does-not-exist"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/generate_204"));
}

void test_a_missing_url_goes_to_the_gate(void) {
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked(nullptr));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked(""));
}

// --- Sloppy matching -----------------------------------------------------------------
//
// A prefix compare on the allowlisted paths would open all of these. They are the
// difference between an allowlist and a suggestion.

void test_allowlisted_paths_match_exactly_not_by_prefix(void) {
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/auth/status-extra"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/auth/statuses"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/auth/change-password-not-really"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/healthcheck"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/health/detail"));
}

void test_the_root_allowlist_entry_does_not_open_every_page(void) {
    // "/" is an exact match, so it must not act as a prefix for the entire site.
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/configuration"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/system/restart"));
}

void test_asset_directories_do_not_leak_to_lookalike_paths(void) {
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/cssx/a.css"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/jsx/a.js"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/css"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/js"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/css/anything"));
}

void test_favicon_matches_exactly(void) {
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/favicon.svg"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/favicon.ico"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/favicon.svg.bak"));
}

// --- The SoftAP widening -------------------------------------------------------------
//
// These are the regression. The provisioning carve-out in customserver.cpp only covers
// UNPROVISIONED, so in GRACE and AP_ASSIST the provisioning routes run the full chain and
// this guard sees them. Without the widening, a meter on the default password whose
// router changed raises its assist AP, refuses the credentials endpoint, and has no way
// back onto a network - the physical button resets the password TO the default, so it
// cannot rescue it either.

void test_provisioning_is_reachable_from_the_ap_while_locked_down(void) {
    TEST_ASSERT_EQUAL(Action::ALLOW, lockedOnAp("/wifi-setup.html"));
    TEST_ASSERT_EQUAL(Action::ALLOW, lockedOnAp("/api/v1/network/wifi/status"));
    TEST_ASSERT_EQUAL(Action::ALLOW, lockedOnAp("/api/v1/network/wifi/scan"));
    TEST_ASSERT_EQUAL(Action::ALLOW, lockedOnAp("/api/v1/network/wifi/diagnostics"));
    TEST_ASSERT_EQUAL(Action::ALLOW, lockedOnAp("/api/v1/network/wifi/credentials"));
}

// The widening is the whole point, so it must not reach past the AP onto the LAN.
void test_the_provisioning_widening_does_not_apply_on_the_lan(void) {
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/wifi-setup.html"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/network/wifi/status"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/network/wifi/scan"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/network/wifi/diagnostics"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/network/wifi/credentials"));
}

// wifi-provisioning: "The OTA endpoint SHALL require authentication in every provisioning
// state and on every netif, including the SoftAP." Being on the AP widens provisioning,
// and provisioning only.
void test_the_ap_never_widens_to_firmware_update_or_anything_else(void) {
    TEST_ASSERT_EQUAL(Action::DENY, lockedOnAp("/api/v1/ota/upload"));
    TEST_ASSERT_EQUAL(Action::DENY, lockedOnAp("/api/v1/ota/rollback"));
    TEST_ASSERT_EQUAL(Action::DENY, lockedOnAp("/api/v1/system/factory-reset"));
    TEST_ASSERT_EQUAL(Action::DENY, lockedOnAp("/api/v1/auth/reset-password"));
    TEST_ASSERT_EQUAL(Action::DENY, lockedOnAp("/api/v1/system/info"));
    TEST_ASSERT_EQUAL(Action::DENY, lockedOnAp("/api/v1/network/config"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, lockedOnAp("/configuration"));
}

void test_the_ap_widening_matches_exactly_not_by_prefix(void) {
    TEST_ASSERT_EQUAL(Action::DENY, lockedOnAp("/api/v1/network/wifi/credentials-extra"));
    TEST_ASSERT_EQUAL(Action::DENY, lockedOnAp("/api/v1/network/wifi/"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, lockedOnAp("/wifi-setup.html.bak"));
}

// A device whose password has been changed is unaffected on the AP too.
void test_the_ap_changes_nothing_once_the_password_is_changed(void) {
    TEST_ASSERT_EQUAL(Action::ALLOW, evaluate(false, true, "/api/v1/ota/upload"));
    TEST_ASSERT_EQUAL(Action::ALLOW, evaluate(false, true, "/api/v1/system/info"));
}

void test_a_missing_url_on_the_ap_still_goes_to_the_gate(void) {
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, lockedOnAp(nullptr));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, lockedOnAp(""));
}

// Trailing slashes are a distinct path, and the allowlist is exact. ESPAsyncWebServer's
// handler matching treats "/x/..." as matching "/x", so these must not ALLOW - otherwise
// "/api/v1/auth/status/" would be waved through to whatever handler claims it.
void test_trailing_slashes_are_not_allowlisted(void) {
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/auth/status/"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/auth/change-password/"));
    TEST_ASSERT_EQUAL(Action::DENY, locked("/api/v1/health/"));
    TEST_ASSERT_EQUAL(Action::DENY, lockedOnAp("/api/v1/network/wifi/credentials/"));
}

// Documents a known asymmetry rather than asserting it is ideal: the "/api/" default-deny
// prefix is case-sensitive, so an uppercase API path falls through to REDIRECT_TO_ROOT
// instead of DENY. Harmless today because handler matching is case-sensitive too, so such a
// request reaches no handler either way - but if anyone enables case-insensitive routing or
// adds a rewrite, this test is where the mismatch surfaces.
void test_uppercase_api_paths_redirect_rather_than_deny(void) {
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("/API/v1/system/info"));
    TEST_ASSERT_EQUAL(Action::REDIRECT_TO_ROOT, locked("//api/v1/system/info"));
}

// The gate page must be able to fetch everything it references. If someone adds an /img/ or
// /fonts/ asset to password-setup.html, this is the test that should fail.
void test_every_asset_the_gate_page_loads_is_reachable(void) {
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/css/button.css"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/css/styles.css"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/css/section.css"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/css/typography.css"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/css/forms.css"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/css/index.css"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/favicon.svg"));
    // and the two endpoints its script calls
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/api/v1/auth/status"));
    TEST_ASSERT_EQUAL(Action::ALLOW, locked("/api/v1/auth/change-password"));
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_a_changed_password_allows_everything);
    RUN_TEST(test_a_changed_password_allows_even_a_null_url);

    RUN_TEST(test_the_way_out_is_open);
    RUN_TEST(test_the_gate_page_and_its_assets_are_open);
    RUN_TEST(test_the_liveness_probe_is_never_blocked);

    RUN_TEST(test_ordinary_api_routes_are_denied);
    RUN_TEST(test_the_dangerous_routes_are_denied);
    RUN_TEST(test_an_unknown_api_route_is_denied);

    RUN_TEST(test_page_routes_go_to_the_gate);
    RUN_TEST(test_an_unknown_page_goes_to_the_gate);
    RUN_TEST(test_a_missing_url_goes_to_the_gate);

    RUN_TEST(test_allowlisted_paths_match_exactly_not_by_prefix);
    RUN_TEST(test_the_root_allowlist_entry_does_not_open_every_page);
    RUN_TEST(test_asset_directories_do_not_leak_to_lookalike_paths);
    RUN_TEST(test_favicon_matches_exactly);

    RUN_TEST(test_provisioning_is_reachable_from_the_ap_while_locked_down);
    RUN_TEST(test_the_provisioning_widening_does_not_apply_on_the_lan);
    RUN_TEST(test_the_ap_never_widens_to_firmware_update_or_anything_else);
    RUN_TEST(test_the_ap_widening_matches_exactly_not_by_prefix);
    RUN_TEST(test_the_ap_changes_nothing_once_the_password_is_changed);
    RUN_TEST(test_a_missing_url_on_the_ap_still_goes_to_the_gate);
    RUN_TEST(test_trailing_slashes_are_not_allowlisted);
    RUN_TEST(test_uppercase_api_paths_redirect_rather_than_deny);
    RUN_TEST(test_every_asset_the_gate_page_loads_is_reachable);

    return UNITY_END();
}
