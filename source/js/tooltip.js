// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

/**
 * Shared info-tooltip logic.
 * Included in every page alongside tooltip.css.
 *
 * Usage: onclick="toggleTooltip(this, event)"
 *
 * The popover is cloned into <body> on open so that position:fixed
 * is always relative to the viewport, regardless of any CSS transform
 * on ancestor elements (e.g. .info-card hover animation).
 */

let _activePopover = null;
let _activeSource  = null;

const MOBILE_BREAKPOINT = 480;
const POPOVER_ARROW_THRESHOLD = 120;

function toggleTooltip(iconButton, event) {
    if (event) event.stopPropagation();

    const source = iconButton.nextElementSibling;
    if (!source || !source.classList.contains('info-tooltip-popover')) return;

    // Clicking the same icon again → close
    if (_activeSource === source) {
        _closeTooltip();
        return;
    }

    _closeTooltip();

    // Clone into <body> to escape any transformed ancestor
    const clone = source.cloneNode(true);
    clone.classList.add('open');
    document.body.appendChild(clone);

    _positionTooltip(clone, iconButton);

    _activePopover = clone;
    _activeSource  = source;

    // Register close listeners on the next tick so this click doesn't immediately close
    setTimeout(function () {
        document.addEventListener('click',  _closeTooltip, { capture: true, once: true });
        document.addEventListener('scroll', _closeTooltip, { capture: true, once: true });
    }, 0);
}

function _positionTooltip(popover, anchor) {
    const popoverWidth = 260; // Should match max-width in .info-tooltip-popover in tooltip.css
    const gap = 8;

    // Mobile: CSS bottom-sheet rules handle layout
    if (window.innerWidth <= MOBILE_BREAKPOINT) return;

    const rect = anchor.getBoundingClientRect();
    const arrowUp = rect.top < POPOVER_ARROW_THRESHOLD;

    let left = rect.left + rect.width / 2 - popoverWidth / 2;
    left = Math.max(8, Math.min(left, window.innerWidth - popoverWidth - 8));

    popover.style.width = popoverWidth + 'px';
    popover.style.left  = left + 'px';

    if (arrowUp) {
        popover.style.top       = (rect.bottom + gap) + 'px';
        popover.style.transform = '';
    } else {
        popover.style.top       = (rect.top - gap) + 'px';
        popover.style.transform = 'translateY(-100%)';
    }

    popover.classList.toggle('arrow-up', arrowUp);
}

function _closeTooltip() {
    if (_activePopover) {
        _activePopover.remove();
        _activePopover = null;
        _activeSource  = null;
    }
    // Clean up listeners (safe to call even if already removed)
    document.removeEventListener('click',  _closeTooltip, { capture: true });
    document.removeEventListener('scroll', _closeTooltip, { capture: true });
}
