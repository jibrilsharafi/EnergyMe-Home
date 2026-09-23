// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "product_line.h"

#include <stdio.h>
#include <string.h>

const char* productLineToString(ProductLine product) {
    switch (product) {
        case ProductLine::HOME:     return PRODUCT_LINE_HOME_STR;
        case ProductLine::HOMEPRO: return PRODUCT_LINE_HOMEPRO_STR;
    }
    return PRODUCT_LINE_HOME_STR;
}

bool parseProductLineString(const char* s, ProductLine& productOut) {
    if (s == nullptr) return false;
    if (strcmp(s, PRODUCT_LINE_HOME_STR) == 0) {
        productOut = ProductLine::HOME;
        return true;
    }
    if (strcmp(s, PRODUCT_LINE_HOMEPRO_STR) == 0) {
        productOut = ProductLine::HOMEPRO;
        return true;
    }
    return false;
}

bool productFromArtifactName(const char* name, ProductLine& productOut) {
    if (name == nullptr) return false;
    if (strstr(name, FIRMWARE_ARTIFACT_TOKEN_HOMEPRO) != nullptr) {
        productOut = ProductLine::HOMEPRO;
        return true;
    }
    if (strstr(name, FIRMWARE_ARTIFACT_TOKEN_HOME) != nullptr) {
        productOut = ProductLine::HOME;
        return true;
    }
    return false;
}

bool parsePcbRevision(const char* s, uint8_t& versionOut) {
    if (s == nullptr || s[0] != 'v') return false;
    unsigned int major = 0;
    unsigned int minor = 0;
    int matched = sscanf(s, "v%u.%u", &major, &minor);
    if (matched != 2) return false;
    if (major > 25 || minor > 9) return false; // keep (major*10+minor) within uint8_t
    versionOut = static_cast<uint8_t>(major * 10 + minor);
    return true;
}
