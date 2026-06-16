# Info Tooltip System Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add reusable `ⓘ` info tooltip icons to any page label, starting with ADE7953 "Failed Readings" on `info.html`, that work on both desktop (hover/click) and mobile (tap).

**Architecture:** A new `tooltip.css` file defines the `.info-tooltip` component with `position: fixed` (JS-positioned) to avoid being clipped by parent containers. A new `tooltip.js` shared script handles click-to-toggle, viewport-aware positioning, and click-outside-to-close. Both files are included in all pages so the pattern is immediately reusable everywhere.

**Tech Stack:** Vanilla CSS + vanilla JS (no dependencies). Follows existing patterns: `#333` dark tooltip, `6px` border-radius, `0.3s` transition, Trebuchet MS font.

---

### Task 1: Create `tooltip.css`

**Files:**
- Create: `source/css/tooltip.css`

**Step 1: Create the file**

```css
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2025 Jibril Sharafi */

/* ───────────────────────────────────────────
   Info Tooltip Component
   Usage:
     <span class="info-tooltip">
       <button class="info-tooltip-icon"
               onclick="toggleTooltip(this)"
               aria-label="More info">ⓘ</button>
       <div class="info-tooltip-popover" role="tooltip">
         Explanation text here.
       </div>
     </span>
   ─────────────────────────────────────────── */

.info-tooltip {
    display: inline-flex;
    align-items: center;
    position: relative;
    margin-left: 5px;
    vertical-align: middle;
}

.info-tooltip-icon {
    background: none;
    border: none;
    padding: 0;
    cursor: pointer;
    font-size: 13px;
    line-height: 1;
    color: #aaa;
    transition: color 0.2s ease;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 16px;
    height: 16px;
}

.info-tooltip-icon:hover,
.info-tooltip-icon:focus {
    color: #555;
    outline: none;
}

/* The floating popover — positioned via JS using position:fixed
   so it is never clipped by overflow:hidden parent containers */
.info-tooltip-popover {
    position: fixed;
    z-index: 9000;
    background-color: #333;
    color: #fff;
    font-size: 13px;
    font-family: 'Trebuchet MS', sans-serif;
    line-height: 1.5;
    padding: 10px 14px;
    border-radius: 6px;
    max-width: 260px;
    box-shadow: 0 4px 16px rgba(0, 0, 0, 0.3);
    pointer-events: none;
    opacity: 0;
    visibility: hidden;
    transition: opacity 0.2s ease, visibility 0.2s ease;
    /* Ensure text wraps cleanly */
    white-space: normal;
    word-wrap: break-word;
}

.info-tooltip-popover.open {
    opacity: 1;
    visibility: visible;
    pointer-events: auto;
}

/* Small arrow pointing down toward the icon (default: popover above icon) */
.info-tooltip-popover::after {
    content: '';
    position: absolute;
    top: 100%;
    left: 50%;
    transform: translateX(-50%);
    border: 6px solid transparent;
    border-top-color: #333;
}

/* When popover is below the icon, arrow points up */
.info-tooltip-popover.arrow-up::after {
    top: auto;
    bottom: 100%;
    border-top-color: transparent;
    border-bottom-color: #333;
}

/* Mobile: full-width bottom sheet style instead of floating */
@media screen and (max-width: 480px) {
    .info-tooltip-popover {
        position: fixed !important;
        left: 12px !important;
        right: 12px !important;
        bottom: 20px !important;
        top: auto !important;
        max-width: none;
        width: auto;
        border-radius: 10px;
        font-size: 14px;
        padding: 14px 16px;
    }

    /* Hide arrow on mobile bottom sheet */
    .info-tooltip-popover::after {
        display: none;
    }
}
```

**Step 2: Verify the file exists**

```bash
ls source/css/tooltip.css
```

Expected: file listed.

**Step 3: Commit**

```bash
git add source/css/tooltip.css
git commit -m "feat: add info-tooltip CSS component"
```

---

### Task 2: Create `tooltip.js`

**Files:**
- Create: `source/js/tooltip.js`

**Step 1: Create the file**

```javascript
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

/**
 * Shared info-tooltip logic.
 * Included in every page alongside tooltip.css.
 *
 * toggleTooltip(iconButton) — call from onclick="toggleTooltip(this)"
 */

let _activeTooltip = null;

function toggleTooltip(iconButton) {
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
```

**Step 2: Verify the file exists**

```bash
ls source/js/tooltip.js
```

Expected: file listed.

**Step 3: Commit**

```bash
git add source/js/tooltip.js
git commit -m "feat: add info-tooltip JS positioning logic"
```

---

### Task 3: Add tooltip to `info.html` (ADE7953 failed readings)

**Files:**
- Modify: `source/html/info.html`

**Step 1: Add `<link>` and `<script>` to `<head>` of `info.html`**

In the `<head>` block, after the existing CSS links and before `</head>`, add:

```html
    <link rel="stylesheet" type="text/css" href="/css/tooltip.css">
```

And before `</body>`, add:

```html
    <script src="/js/tooltip.js"></script>
```

**Step 2: Add tooltip to "Failed Readings" label**

Find this block in `info.html` (inside the ADE7953 card):

```html
                <div class="info-item">
                    <span class="info-label"><span class="emoji">❌</span>Failed Readings</span>
                    <span class="info-value" id="ade7953ReadingFailures">Loading...</span>
                </div>
```

Replace with:

```html
                <div class="info-item">
                    <span class="info-label">
                        <span class="emoji">❌</span>Failed Readings
                        <span class="info-tooltip">
                            <button class="info-tooltip-icon" onclick="toggleTooltip(this)" aria-label="More info about failed readings">ⓘ</button>
                            <div class="info-tooltip-popover" role="tooltip">
                                A failed reading occurs when the ADE7953 energy meter chip does not respond correctly over SPI. A small number is normal on startup; persistently high counts may indicate hardware or wiring issues.
                            </div>
                        </span>
                    </span>
                    <span class="info-value" id="ade7953ReadingFailures">Loading...</span>
                </div>
```

**Step 3: Test manually**

Open `info.html` in a browser (or via the device's web server). Click the `ⓘ` icon next to "Failed Readings":
- Desktop: popover floats above (or below if near top of viewport), not clipped by the card
- Mobile (narrow): popover appears as a bottom sheet
- Clicking outside closes it

**Step 4: Commit**

```bash
git add source/html/info.html
git commit -m "feat: add info tooltip to ADE7953 failed readings"
```

---

### Task 4: Include tooltip files in all other pages (optional but recommended)

**Files:**
- Modify each: `source/html/index.html`, `channel.html`, `configuration.html`, `calibration.html`, `integrations.html`, `update.html`, `log.html`, `waveform.html`, `ade7953-tester.html`, `swagger.html`

**Step 1: For each HTML file, add the CSS link to `<head>`**

After existing CSS `<link>` tags:
```html
    <link rel="stylesheet" type="text/css" href="/css/tooltip.css">
```

And the JS script before `</body>`:
```html
    <script src="/js/tooltip.js"></script>
```

**Step 2: Commit**

```bash
git add source/html/*.html
git commit -m "feat: include tooltip CSS/JS in all pages"
```

---

## Verification

End-to-end test checklist:

- [ ] `ⓘ` icon appears inline next to "Failed Readings" label, not disrupting layout
- [ ] Clicking `ⓘ` opens the popover above the icon (or below if near screen top)
- [ ] Popover is not clipped by the `.info-card` container
- [ ] Clicking outside closes the popover
- [ ] On mobile (< 480px): popover appears as a bottom sheet
- [ ] Other cards are unaffected
- [ ] Page loads without JS errors in console
- [ ] Adding another `ⓘ` elsewhere requires only the HTML snippet (no new CSS/JS needed)
