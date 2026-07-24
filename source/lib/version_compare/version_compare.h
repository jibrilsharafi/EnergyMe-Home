// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

// Pure, dependency-free "X.Y.Z" / "vX.Y.Z" / "VX.Y.Z" version comparator.
//
// Missing/unparseable components (a null pointer, empty string, non-numeric
// text, or a component past the first two "." separators) are treated as 0,
// so a malformed or absent version string degrades to "0.0.0" rather than
// crashing or triggering undefined behavior. This is a deliberate fail-safe:
// callers using this to gate an action (e.g. "reject if not newer") will
// reject malformed input rather than silently accept it.
//
// Each component is parsed with strtol (not sscanf's "%d", which is
// undefined behavior on overflow) and clamped to [0, INT_MAX]: negative
// numbers degrade to 0, out-of-range numbers clamp to INT_MAX. Comparison
// is a three-way compare, not subtraction, so it can never wrap around.

namespace VersionCompare {

// Returns >0 if `current` is newer than `available`, 0 if equal, <0 if older.
int compare(const char *current, const char *available);

}  // namespace VersionCompare
