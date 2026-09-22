// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Unit tests for the reboot wait screen's poll loop (source/js/reboot-wait.js).
// Run: node --test source/test/js/

const test = require('node:test');
const assert = require('node:assert/strict');
const { RebootWait } = require('../../js/reboot-wait.js');

// Poll loop with the DOM, timers and network stubbed: polls answer from `answers`
// (the last one repeats), the cap is short so a never-ending success run ends in the fallback.
function runLoop(answers, requireFailureFirst) {
    global.window = { location: { href: '/update' } };
    const wait = new RebootWait();
    wait.MAX_WAIT_MS = 200;
    wait.POLL_INTERVAL_MS = 1;
    wait._delay = () => new Promise(resolve => setTimeout(resolve, 1));
    wait._setStatus = () => {};
    wait._stopElapsedTimer = () => {};
    wait._stopTrivia = () => {};
    let polls = 0;
    wait._pollHealth = async () => answers[Math.min(polls++, answers.length - 1)];
    let fallback = false;
    wait._showFallback = () => { fallback = true; };
    const token = ++wait._pollToken;
    return wait._runPollLoop(token, '', '/', requireFailureFirst).then(() => ({ redirected: window.location.href === '/', fallback }));
}

test('redirects once the device went down and came back', async () => {
    assert.deepEqual(await runLoop([true, false, false, true], true), { redirected: true, fallback: false });
});

test('a device that never went down is not taken as rebooted', async () => {
    assert.deepEqual(await runLoop([true], true), { redirected: false, fallback: true });
});

test('check again redirects when the device is already back', async () => {
    assert.deepEqual(await runLoop([true], false), { redirected: true, fallback: false });
});

test('the retry button re-polls without the went-down gate', () => {
    global.window = { location: { href: '/update' } };
    const wait = new RebootWait();
    let options = null;
    wait.show = opts => { options = opts; };
    wait._stopElapsedTimer = () => {};
    wait._stopTrivia = () => {};
    wait._els = { waitContent: { style: {} }, fallback: { style: {} }, retryBtn: {} };
    wait._showFallback(1, '', '/');
    wait._els.retryBtn.onclick();
    assert.equal(options.requireFailureFirst, false);
});
