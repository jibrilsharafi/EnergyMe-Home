// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

// ECDSA P-256 public key used by Mqtt::_verifyOtaSignature() to verify OTA
// firmware signatures before the boot partition is switched. Compiled in,
// not NVS-stored, so it cannot be altered without reflashing.
//
// Public half of an asymmetric KMS signing key; the private half never
// leaves KMS. Gated per build so dev and prod trust different keys.

#ifndef ENV_DEV
// Production key.
constexpr const char* OTA_SIGNING_PUBLIC_KEY_PEM =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEFtOoSAhpqnoaVbqMTTGsc3t0nMaMp0raYmcJId22\n"
"bzF37RlPSgXIqXRlwUhxhRWJzwVHhouE/hqUWdL3rmdwMg==\n"
"-----END PUBLIC KEY-----\n";
#else
// Dev/test key - dev builds only, never reaches a vendor device.
constexpr const char* OTA_SIGNING_PUBLIC_KEY_PEM =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEL30m5KjXuHbjc7Q36kt023IgGid7\n"
"XH1V0oCPXF2ebIUSY+Pm/tIWXEhVA08SE7ROIwHFWdonsXY0lb3BgnOWww==\n"
"-----END PUBLIC KEY-----\n";
#endif
