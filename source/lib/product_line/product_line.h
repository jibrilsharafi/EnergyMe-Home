// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Product line identification and hardware profile selection - pure logic, no
// Arduino dependencies, so it is host-testable (test/test_product_line).

#pragma once

#include <stddef.h>
#include <stdint.h>

// Product line, read from factory NVS (factory_ns::product_line) at boot.
// Absent key -> HOME, permanently: the deployed fleet predates the key.
// PCB versions are numbered independently per product (Home Pro restarts at v1.0),
// so profile lookup is always keyed by (product, version), never version alone.
enum class ProductLine : uint8_t {
    HOME = 0,
    HOMEPRO = 1,
};

#define PRODUCT_LINE_HOME_STR     "home"
#define PRODUCT_LINE_HOMEPRO_STR "homepro"

const char* productLineToString(ProductLine product);

// Parse a product string ("home" / "homepro") into the enum.
// Returns false for any unknown value, leaving productOut untouched.
bool parseProductLineString(const char* s, ProductLine& productOut);

// Firmware artifact name tokens. Home and Pro binaries are NOT interchangeable
// (quad vs octal PSRAM, fixed at compile time), so every delivery path checks
// the artifact against the running product before flashing.
#define FIRMWARE_ARTIFACT_TOKEN_HOME     "energyme_home"
#define FIRMWARE_ARTIFACT_TOKEN_HOMEPRO "energyme_homepro"

// Identify the product a firmware artifact name was built for. The Home token is
// a substring of the Pro token, so the Pro token is matched FIRST - a plain
// substring check on the Home token alone would accept Pro images on Home.
// Returns false when the name carries no recognizable token (e.g. a self-built
// community image), which callers treat as "unknown", not as a mismatch.
bool productFromArtifactName(const char* name, ProductLine& productOut);

// Parse a pcb_revision string of the form "vMAJOR.MINOR" (e.g. "v6.1") into the
// packed uint8_t used by HardwareProfile::version (major * 10 + minor).
// Returns true on success; false on any parse/format error.
bool parsePcbRevision(const char* s, uint8_t& versionOut);

namespace ProfileSelection {

// Outcome of resolving factory NVS strings to a profile. Anything but SELECTED
// means community mode.
enum class Result : uint8_t {
    SELECTED,
    NO_REVISION,        // pcb_revision absent/empty
    UNKNOWN_PRODUCT,    // product_line present but not a known value
    MALFORMED_REVISION, // pcb_revision not "vMAJOR.MINOR"
    UNKNOWN_PROFILE,    // well-formed, but no profile for (product, version)
};

// Profile is any struct with `product` and `version` members (HardwareProfile on
// device, a stub in the host tests).
template <typename Profile>
const Profile* find(const Profile* profiles, size_t count, ProductLine product, uint8_t version) {
    for (size_t i = 0; i < count; i++) {
        if (profiles[i].product == product && profiles[i].version == version) return &profiles[i];
    }
    return nullptr;
}

// First (= latest) profile of the given product. Entries are ordered newest-first
// within each product, so the first product match is that product's latest.
// Falls back to profiles[0] when the product has no entry at all.
template <typename Profile>
const Profile* latestForProduct(const Profile* profiles, size_t count, ProductLine product) {
    for (size_t i = 0; i < count; i++) {
        if (profiles[i].product == product) return &profiles[i];
    }
    return &profiles[0];
}

// Resolve factory NVS strings (either may be nullptr = key absent) to a profile.
// An absent/empty product_line means HOME. productOut/versionOut are filled as far
// as parsing got, for logging; profileOut is set only on SELECTED.
template <typename Profile>
Result select(const char* pcbRevision, const char* productLine, const Profile* profiles, size_t count,
              const Profile*& profileOut, ProductLine& productOut, uint8_t& versionOut) {
    if (pcbRevision == nullptr || pcbRevision[0] == '\0') return Result::NO_REVISION;
    productOut = ProductLine::HOME;
    if (productLine != nullptr && productLine[0] != '\0' && !parseProductLineString(productLine, productOut)) {
        return Result::UNKNOWN_PRODUCT;
    }
    if (!parsePcbRevision(pcbRevision, versionOut)) return Result::MALFORMED_REVISION;
    const Profile* p = find(profiles, count, productOut, versionOut);
    if (p == nullptr) return Result::UNKNOWN_PROFILE;
    profileOut = p;
    return Result::SELECTED;
}

} // namespace ProfileSelection
