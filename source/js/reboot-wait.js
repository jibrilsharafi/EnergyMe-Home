/* SPDX-License-Identifier: GPL-3.0-or-later
    Copyright (C) 2025 Jibril Sharafi */

/**
 * Shared "device is restarting" wait screen.
 * Polls /api/v1/health instead of guessing a fixed delay: a poll success only
 * counts once at least one poll has already failed, so a health check that
 * lands before the device actually drops off the network doesn't trigger an
 * early redirect. See openspec/changes/add-reboot-wait-modal (or, once
 * archived, openspec/specs/reboot-wait-ui) for the behavior contract.
 */
class RebootWait {
    constructor() {
        this.POLL_INTERVAL_MS = 2000;
        this.POLL_TIMEOUT_MS = 2500;
        this.MIN_POLL_MS = 1000; // a shorter last poll could time out on a device that is back
        this.MAX_WAIT_MS = 90000;
        this.TRIVIA_INTERVAL_MS = 4200;

        // Shared with update.html's OTA upload progress screen, one fact
        // list, not two that drift apart.
        this.FACTS = [
            "The first computer bug was an actual moth found trapped inside a relay in 1947.",
            "You could fit every planet in the solar system in the gap between Earth and the Moon.",
            "China and the USA alone account for almost half of the world's electricity consumption.",
            "The Egyptians are as far in time from the Romans as the Romans are from us.",
            "A running microwave draws more power in a minute than your router uses all day on standby.",
            "James Watt, who the watt is named after, never once saw an electron in his life.",
            "Octopuses have three hearts, and two of them stop beating when they swim.",
            "This firmware is open source, you can go read the code you're waiting on right now."
        ];

        this._els = null; // lazily built DOM refs, reused across show() calls
        this._elapsedSec = 0;
        this._elapsedTimer = null;
        this._triviaIndex = 0;
        this._triviaTimer = null;
        this._pollToken = 0; // bumped on each show() to invalidate a stale poll loop
    }

    /**
     * Show the wait screen and start polling for the device to come back.
     * @param {Object} [options]
     * @param {string} [options.redirectTo] - URL to send the browser to once the device is
     *   back online. Defaults to "/". Pass an absolute URL when the device may answer at a
     *   different address (e.g. a static IP change).
     * @param {string} [options.baseUrl] - Origin to poll health against, when different from
     *   the current page's origin (paired with a cross-origin redirectTo).
     * @param {boolean} [options.requireFailureFirst] - Only treat the device as back after a
     *   failed poll (it went down). Default true; false on "Check again", where the device may
     *   already be back and every poll succeeds.
     */
    show(options = {}) {
        const redirectTo = options.redirectTo || '/';
        const requireFailureFirst = options.requireFailureFirst !== false;
        const baseUrl = (options.baseUrl || '').replace(/\/$/, '');
        const token = ++this._pollToken;
        // The cap counts from the trigger, so neither the pre-delay nor a pending poll pushes
        // the fallback past it
        const deadline = this._now() + this.MAX_WAIT_MS;

        this._mount();
        this._showWaitContent();
        this._setStatus("Poking the device, asking it to restart...");
        this._startElapsedTimer();
        this._startTrivia();

        this._runPollLoop(token, baseUrl, redirectTo, requireFailureFirst, deadline);
    }

    async _runPollLoop(token, baseUrl, redirectTo, requireFailureFirst, deadline = this._now() + this.MAX_WAIT_MS) {
        const remaining = () => Math.max(0, deadline - this._now());
        await this._delay(Math.min(900, remaining()));
        if (token !== this._pollToken) return;
        this._setStatus("It's off rebooting somewhere, hang tight!");

        let sawFailure = !requireFailureFirst;

        while (remaining() >= this.MIN_POLL_MS) {
            if (token !== this._pollToken) return;

            const ok = await this._pollHealth(baseUrl, Math.min(this.POLL_TIMEOUT_MS, remaining()));
            if (token !== this._pollToken) return;

            if (ok && sawFailure) {
                this._setStatus("Oh hey, it's back! Taking you home now");
                this._stopElapsedTimer();
                this._stopTrivia();
                await this._delay(1200);
                if (token !== this._pollToken) return;
                window.location.href = redirectTo;
                return;
            }
            if (!ok) sawFailure = true;

            await this._delay(Math.min(this.POLL_INTERVAL_MS, remaining()));
        }

        if (token === this._pollToken) this._showFallback(token, baseUrl, redirectTo);
    }

    _pollHealth(baseUrl, timeoutMs) {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), timeoutMs);
        // no-cors: the device sends no Access-Control-Allow-Origin header, so a same-origin
        // restart still works in normal mode but a cross-origin one (static IP / DHCP address
        // change) would have every poll rejected by the browser's CORS check regardless of
        // whether the device is actually up. no-cors sidesteps that; the response becomes
        // opaque (status unreadable) either way, but reachability is all this needs - the
        // health endpoint has no failure response to distinguish, only up or unreachable.
        return fetch(`${baseUrl}/api/v1/health`, { signal: controller.signal, cache: 'no-store', mode: 'no-cors' })
            .then(() => true)
            .catch(() => false)
            .finally(() => clearTimeout(timeoutId));
    }

    _now() {
        return Date.now();
    }

    _delay(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    // ---- DOM ----

    _mount() {
        if (this._els) return;

        const scrim = document.createElement('div');
        scrim.className = 'reboot-wait-scrim';
        scrim.innerHTML = `
            <div class="reboot-wait-box" role="dialog" aria-modal="true" aria-label="Device restarting" tabindex="-1">
                <div class="reboot-wait-wait-content">
                    <div class="reboot-wait-spinner"></div>
                    <p class="reboot-wait-status" aria-live="polite"></p>
                    <p class="reboot-wait-elapsed"><span class="reboot-wait-elapsed-value">0s</span> and counting</p>
                    <div class="reboot-wait-trivia">
                        <button type="button" class="reboot-wait-trivia-arrow" data-dir="-1" aria-label="Previous fact">‹</button>
                        <div class="reboot-wait-trivia-body">
                            <span class="reboot-wait-trivia-label">Did you know?</span>
                            <p class="reboot-wait-trivia-text"></p>
                        </div>
                        <button type="button" class="reboot-wait-trivia-arrow" data-dir="1" aria-label="Next fact">›</button>
                    </div>
                </div>
                <div class="reboot-wait-fallback" style="display: none;">
                    <p>Still not back online. It might need a manual check.</p>
                    <div class="buttonForm-container">
                        <button type="button" class="buttonForm reboot-wait-retry">Check again</button>
                        <button type="button" class="buttonForm reboot-wait-home">Go to homepage</button>
                    </div>
                </div>
            </div>`;
        document.body.appendChild(scrim);

        this._els = {
            scrim,
            waitContent: scrim.querySelector('.reboot-wait-wait-content'),
            status: scrim.querySelector('.reboot-wait-status'),
            elapsed: scrim.querySelector('.reboot-wait-elapsed-value'),
            triviaText: scrim.querySelector('.reboot-wait-trivia-text'),
            fallback: scrim.querySelector('.reboot-wait-fallback'),
            retryBtn: scrim.querySelector('.reboot-wait-retry'),
            homeBtn: scrim.querySelector('.reboot-wait-home'),
            box: scrim.querySelector('.reboot-wait-box')
        };

        scrim.querySelectorAll('.reboot-wait-trivia-arrow').forEach(btn => {
            btn.addEventListener('click', () => {
                this._showTrivia(Number(btn.dataset.dir));
                this._restartTriviaClock();
            });
        });

        this._els.homeBtn.addEventListener('click', () => { window.location.href = '/'; });
    }

    _showWaitContent() {
        // The scrim only blocks the pointer: inert keeps Tab and screen readers off the page
        // underneath (re-applied per show, for toasts added since). Never undone: the screen
        // ends in a navigation.
        Array.from(document.body.children).forEach(el => { if (el !== this._els.scrim) el.inert = true; });
        this._els.scrim.style.display = 'flex';
        this._els.waitContent.style.display = 'block';
        this._els.fallback.style.display = 'none';
        this._els.box.focus();
    }

    _showFallback(token, baseUrl, redirectTo) {
        this._stopElapsedTimer();
        this._stopTrivia();
        this._els.waitContent.style.display = 'none';
        this._els.fallback.style.display = 'block';
        this._els.retryBtn.onclick = () => {
            this._pollToken++; // invalidate anything left of the old loop, just in case
            this.show({ redirectTo, baseUrl: baseUrl || undefined, requireFailureFirst: false });
        };
    }

    _setStatus(text) {
        this._els.status.textContent = text;
    }

    // ---- elapsed timer ----

    _startElapsedTimer() {
        this._elapsedSec = 0;
        this._paintElapsed();
        this._stopElapsedTimer();
        this._elapsedTimer = setInterval(() => {
            this._elapsedSec += 1;
            this._paintElapsed();
        }, 1000);
    }

    _stopElapsedTimer() {
        clearInterval(this._elapsedTimer);
        this._elapsedTimer = null;
    }

    _paintElapsed() {
        this._els.elapsed.textContent = `${this._elapsedSec}s`;
    }

    // ---- trivia carousel ----

    _startTrivia() {
        this._triviaIndex = Math.floor(Math.random() * this.FACTS.length);
        this._paintTrivia();
        this._restartTriviaClock();
    }

    _stopTrivia() {
        clearInterval(this._triviaTimer);
        this._triviaTimer = null;
    }

    _restartTriviaClock() {
        clearInterval(this._triviaTimer);
        if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) return;
        this._triviaTimer = setInterval(() => this._showTrivia(1), this.TRIVIA_INTERVAL_MS);
    }

    _showTrivia(delta) {
        this._triviaIndex = (this._triviaIndex + delta + this.FACTS.length) % this.FACTS.length;
        this._paintTrivia();
    }

    _paintTrivia() {
        this._els.triviaText.textContent = this.FACTS[this._triviaIndex];
    }
}

// Browser singleton, or a module for the node unit tests
if (typeof window !== 'undefined') window.rebootWait = new RebootWait();
if (typeof module !== 'undefined') module.exports = { RebootWait };
