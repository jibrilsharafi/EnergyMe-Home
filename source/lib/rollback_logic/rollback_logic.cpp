// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "rollback_logic.h"

#include <cctype>
#include <cstring>

namespace RollbackLogic {

bool isValidSha256Hex(const char *sha) {
    if (!sha) return false;
    for (size_t i = 0; i < SHA256_HEX_LEN; i++) {
        if (!isxdigit(static_cast<unsigned char>(sha[i]))) return false;
    }
    return sha[SHA256_HEX_LEN] == '\0';
}

bool sha256HexEquals(const char *a, const char *b) {
    if (!isValidSha256Hex(a) || !isValidSha256Hex(b)) return false;
    for (size_t i = 0; i < SHA256_HEX_LEN; i++) {
        if (tolower(static_cast<unsigned char>(a[i])) !=
            tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

Decision decide(const char *expectedSha,
                bool passiveReadable,
                const char *passiveSha,
                const char *runningSha) {
    if (!isValidSha256Hex(expectedSha)) return Decision::MISSING_SHA;

    // Redelivery check first: after a completed rollback the passive slot
    // holds the image we just left, so a re-delivered command's expected sha
    // matches the now-running slot. Must win over NO_TARGET/MISMATCH so a
    // QoS1 duplicate can never switch the slots back.
    if (sha256HexEquals(expectedSha, runningSha)) return Decision::NOOP_ALREADY_DONE;

    if (!passiveReadable) return Decision::NO_TARGET;
    if (sha256HexEquals(expectedSha, passiveSha)) return Decision::PROCEED;
    return Decision::MISMATCH;
}

}  // namespace RollbackLogic
