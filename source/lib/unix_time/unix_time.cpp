// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "unix_time.h"

namespace UnixTime {

bool isValid(uint64_t unixTime, bool isMilliseconds) {
    if (isMilliseconds) {
        return unixTime >= MIN_MILLISECONDS && unixTime <= MAX_MILLISECONDS;
    }
    return unixTime >= MIN_SECONDS && unixTime <= MAX_SECONDS;
}

} // namespace UnixTime
