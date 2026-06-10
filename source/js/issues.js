/* SPDX-License-Identifier: GPL-3.0-or-later
    Copyright (C) 2025 Jibril Sharafi */

/**
 * EnergyMe device issues widget (issue #145).
 * Self-contained: injects a badge fixed to the top-right corner on every page
 * that includes it (after api-client.js), polls /api/v1/system/issues and
 * opens an overlay panel with the issue list and acknowledge actions.
 * Dispatches a window 'energyme-issues' CustomEvent on every poll so pages
 * can render their own channel-scoped chips (see channel.html).
 */
(function () {
    'use strict';

    const POLL_INTERVAL_MS = 10000;
    const SEVERITY_ORDER = { info: 0, warning: 1, error: 2 };
    const SEVERITY_ICON = { info: 'ℹ️', warning: '⚠️', error: '🔴' };

    let lastData = null;
    let panelOpen = false;

    const style = document.createElement('style');
    style.textContent = `
        .issues-badge {
            position: fixed; top: 12px; right: 12px; z-index: 1000;
            display: none; align-items: center; gap: 6px;
            padding: 6px 12px; border-radius: 16px; cursor: pointer;
            font-family: inherit; font-size: 14px; font-weight: bold; color: #fff;
            box-shadow: 0 2px 6px rgba(0,0,0,0.25); user-select: none;
        }
        .issues-badge.error { background-color: #dc3545; }
        .issues-badge.warning { background-color: #fd7e14; }
        .issues-badge.info { background-color: #0d6efd; }
        .issues-badge.acked { background-color: #6c757d; }
        .issues-overlay {
            position: fixed; inset: 0; z-index: 1001;
            display: none; background: rgba(0,0,0,0.4);
        }
        .issues-panel {
            position: absolute; top: 50px; right: 12px;
            width: min(440px, calc(100vw - 24px)); max-height: 75vh; overflow-y: auto;
            background: #fff; border-radius: 8px; box-shadow: 0 4px 16px rgba(0,0,0,0.3);
            padding: 12px; font-size: 14px; text-align: left;
        }
        .issues-panel h3 { margin: 4px 4px 10px 4px; font-size: 16px; }
        .issues-item {
            border: 1px solid #e0e0e0; border-left-width: 4px; border-radius: 6px;
            padding: 8px 10px; margin-bottom: 8px; background: #fafafa;
        }
        .issues-item.error { border-left-color: #dc3545; }
        .issues-item.warning { border-left-color: #fd7e14; }
        .issues-item.info { border-left-color: #0d6efd; }
        .issues-item.resolved { opacity: 0.65; }
        .issues-item-title { font-weight: bold; margin-bottom: 4px; }
        .issues-item-message { margin-bottom: 6px; word-break: break-word; }
        .issues-item-meta { color: #666; font-size: 12px; margin-bottom: 6px; }
        .issues-ack-button {
            padding: 3px 10px; border: 1px solid #6c757d; border-radius: 4px;
            background: #fff; color: #333; cursor: pointer; font-size: 12px;
        }
        .issues-ack-button:hover { background: #f0f0f0; }
        .issues-ack-all { margin-bottom: 10px; }
        .issues-empty { color: #666; padding: 8px 4px; }
        .issues-chip {
            display: inline-flex; align-items: center; gap: 4px; cursor: pointer;
            padding: 2px 8px; border-radius: 10px; font-size: 12px; font-weight: bold;
            color: #fff; vertical-align: middle; margin-left: 6px;
        }
        .issues-chip.error { background-color: #dc3545; }
        .issues-chip.warning { background-color: #fd7e14; }
        .issues-chip.info { background-color: #0d6efd; }
        .issues-chip.acked { background-color: #6c757d; }
    `;
    document.head.appendChild(style);

    const badge = document.createElement('div');
    badge.className = 'issues-badge';
    badge.title = 'Device issues';
    badge.addEventListener('click', openPanel);

    const overlay = document.createElement('div');
    overlay.className = 'issues-overlay';
    overlay.addEventListener('click', (event) => {
        if (event.target === overlay) closePanel();
    });

    const panel = document.createElement('div');
    panel.className = 'issues-panel';
    overlay.appendChild(panel);

    function attachElements() {
        document.body.appendChild(badge);
        document.body.appendChild(overlay);
    }
    if (document.body) attachElements();
    else document.addEventListener('DOMContentLoaded', attachElements);

    function severityName(issue) {
        return SEVERITY_ORDER[issue.severity] !== undefined ? issue.severity : 'info';
    }

    function isUnacked(issue) {
        return issue.state === 'active_unacked' || issue.state === 'cleared_unacked';
    }

    function formatTime(unixSeconds) {
        if (!unixSeconds) return 'unknown';
        return new Date(unixSeconds * 1000).toLocaleString();
    }

    function titleFor(issue) {
        // code is snake_case; humanize it
        const text = issue.code.replace(/_/g, ' ');
        return text.charAt(0).toUpperCase() + text.slice(1);
    }

    function updateBadge(data) {
        const summary = data.summary || {};
        const total = summary.total || 0;
        const unacked = summary.unacked || 0;

        if (total === 0) {
            badge.style.display = 'none';
            return;
        }

        badge.style.display = 'flex';
        badge.className = 'issues-badge ' + (unacked > 0 ? (summary.maxUnackedSeverity || 'info') : 'acked');
        badge.textContent = `${SEVERITY_ICON[summary.maxUnackedSeverity] || '✓'} ${unacked > 0 ? unacked : total}`;
        badge.title = unacked > 0
            ? `${unacked} unacknowledged device issue(s) - click for details`
            : `${total} acknowledged device issue(s) still active - click for details`;
    }

    function renderPanel(data) {
        const issues = (data && data.issues) ? data.issues.slice() : [];
        issues.sort((a, b) => (SEVERITY_ORDER[severityName(b)] - SEVERITY_ORDER[severityName(a)]));

        let html = '<h3>Device issues</h3>';
        if (issues.length === 0) {
            html += '<div class="issues-empty">No active issues. All good! ✨</div>';
        } else {
            if (issues.some(isUnacked)) {
                html += '<button class="issues-ack-button issues-ack-all" data-ack-all="1">Acknowledge all</button>';
            }
            issues.forEach((issue) => {
                const resolved = issue.state === 'cleared_unacked';
                const channelText = issue.channel !== undefined ? ` &middot; channel ${issue.channel}` : '';
                const stateText = resolved ? 'resolved, unseen' : (issue.state === 'active_acked' ? 'active, acknowledged' : 'active');
                html += `
                    <div class="issues-item ${severityName(issue)}${resolved ? ' resolved' : ''}">
                        <div class="issues-item-title">${SEVERITY_ICON[severityName(issue)] || ''} ${titleFor(issue)}</div>
                        <div class="issues-item-message">${issue.message || ''}</div>
                        <div class="issues-item-meta">
                            ${stateText}${channelText} &middot; first seen ${formatTime(issue.firstSeenUnix)}
                            ${issue.occurrences > 1 ? ` &middot; ${issue.occurrences} occurrences` : ''}
                        </div>
                        ${isUnacked(issue) ? `<button class="issues-ack-button" data-code="${issue.code}" data-channel="${issue.channel !== undefined ? issue.channel : ''}">Acknowledge</button>` : ''}
                    </div>`;
            });
        }
        panel.innerHTML = html;

        panel.querySelectorAll('.issues-ack-button').forEach((button) => {
            button.addEventListener('click', async () => {
                try {
                    if (button.dataset.ackAll) {
                        await window.energyApi.post('system/issues/ack', { all: true });
                    } else {
                        const body = { code: button.dataset.code };
                        if (button.dataset.channel !== '') body.channel = parseInt(button.dataset.channel, 10);
                        await window.energyApi.post('system/issues/ack', body);
                    }
                    await poll();
                    if (panelOpen) renderPanel(lastData);
                } catch (error) {
                    console.error('Failed to acknowledge issue:', error);
                    if (typeof showStatus === 'function') showStatus('Failed to acknowledge issue', 'error');
                }
            });
        });
    }

    function openPanel() {
        panelOpen = true;
        renderPanel(lastData);
        overlay.style.display = 'block';
    }

    function closePanel() {
        panelOpen = false;
        overlay.style.display = 'none';
    }

    async function poll() {
        try {
            const data = await window.energyApi.get('system/issues');
            lastData = data;
            updateBadge(data);
            if (panelOpen) renderPanel(data);
            window.dispatchEvent(new CustomEvent('energyme-issues', { detail: data }));
        } catch (error) {
            // Polling failure is not itself an issue worth surfacing; just log it
            console.debug('Issues poll failed:', error);
        }
    }

    // Public hook for pages (e.g. channel chips) to open the panel programmatically
    window.energymeIssues = {
        open: openPanel,
        close: closePanel,
        getData: () => lastData,
        refresh: poll
    };

    function start() {
        poll();
        setInterval(poll, POLL_INTERVAL_MS);
    }
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
    else start();
})();
