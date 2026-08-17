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
     */
    show(options = {}) {
        const redirectTo = options.redirectTo || '/';
        const baseUrl = (options.baseUrl || '').replace(/\/$/, '');
        const token = ++this._pollToken;

        this._mount();
        this._showWaitContent();
        this._setStatus("Poking the device, asking it to restart...");
        this._startElapsedTimer();
        this._startTrivia();

        this._runPollLoop(token, baseUrl, redirectTo);
    }

    async _runPollLoop(token, baseUrl, redirectTo) {
        await this._delay(900);
        if (token !== this._pollToken) return;
        this._setStatus("It's off rebooting somewhere, hang tight!");

        let sawFailure = false;
        const startTime = Date.now();

        while (Date.now() - startTime < this.MAX_WAIT_MS) {
            if (token !== this._pollToken) return;

            const ok = await this._pollHealth(baseUrl);
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

            await this._delay(this.POLL_INTERVAL_MS);
        }

        if (token === this._pollToken) this._showFallback(token, baseUrl, redirectTo);
    }

    _pollHealth(baseUrl) {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), this.POLL_TIMEOUT_MS);
        return fetch(`${baseUrl}/api/v1/health`, { signal: controller.signal, cache: 'no-store' })
            .then(response => {
                clearTimeout(timeoutId);
                return response.ok;
            })
            .catch(() => {
                clearTimeout(timeoutId);
                return false;
            });
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
            <div class="reboot-wait-box">
                <div class="reboot-wait-wait-content">
                    <div class="reboot-wait-spinner"></div>
                    <p class="reboot-wait-status"></p>
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
            homeBtn: scrim.querySelector('.reboot-wait-home')
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
        this._els.scrim.style.display = 'flex';
        this._els.waitContent.style.display = 'block';
        this._els.fallback.style.display = 'none';
    }

    _showFallback(token, baseUrl, redirectTo) {
        this._stopElapsedTimer();
        this._stopTrivia();
        this._els.waitContent.style.display = 'none';
        this._els.fallback.style.display = 'block';
        this._els.retryBtn.onclick = () => {
            this._pollToken++; // invalidate anything left of the old loop, just in case
            this.show({ redirectTo, baseUrl: baseUrl || undefined });
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

window.rebootWait = new RebootWait();
