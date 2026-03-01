// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

/**
 * Shared info-tooltip logic.
 * Included in every page alongside tooltip.css.
 *
 * toggleTooltip(iconButton) — call from onclick="toggleTooltip(this)"
 */

let _activeTooltip = null;

function toggleTooltip(iconButton, event) {
    if (event) event.stopPropagation();

    const popover = iconButton.nextElementSibling;
    if (!popover || !popover.classList.contains('info-tooltip-popover')) return;

    const isOpen = popover.classList.contains('open');

    // Close any currently open tooltip first
    _closeAllTooltips();

    if (!isOpen) {
        _positionTooltip(popover, iconButton);
        popover.classList.add('open');
        _activeTooltip = popover;
    }
}

function _positionTooltip(popover, anchor) {
    // On mobile (<= 480px) CSS handles positioning via bottom-sheet rules
    if (window.innerWidth <= 480) return;

    const rect = anchor.getBoundingClientRect();
    const popoverWidth = 260; // matches max-width in CSS
    const gap = 8; // space between icon and popover

    // Temporarily make visible (off-screen) to measure height
    popover.style.visibility = 'hidden';
    popover.style.opacity = '0';
    popover.style.display = 'block';
    const popoverHeight = popover.offsetHeight || 80;
    popover.style.display = '';
    popover.style.visibility = '';

    // Prefer above the icon
    let top = rect.top - popoverHeight - gap;
    let arrowUp = false;

    // If not enough room above, show below
    if (top < 8) {
        top = rect.bottom + gap;
        arrowUp = true;
    }

    // Horizontal: center on icon, clamp to viewport
    let left = rect.left + rect.width / 2 - popoverWidth / 2;
    left = Math.max(8, Math.min(left, window.innerWidth - popoverWidth - 8));

    popover.style.top = top + 'px';
    popover.style.left = left + 'px';
    popover.classList.toggle('arrow-up', arrowUp);
}

function _closeAllTooltips() {
    document.querySelectorAll('.info-tooltip-popover.open').forEach(p => {
        p.classList.remove('open');
    });
    _activeTooltip = null;
}

// Close on outside click / scroll
document.addEventListener('click', function (e) {
    if (_activeTooltip && !e.target.closest('.info-tooltip')) {
        _closeAllTooltips();
    }
});

document.addEventListener('scroll', _closeAllTooltips, true);
