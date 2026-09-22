// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

// ECDSA P-256 public keys used by Mqtt::_verifyOtaSignature() to verify OTA
// firmware signatures before the boot partition is switched. Compiled in,
// not NVS-stored, so they cannot be altered without reflashing.
//
// Public half of asymmetric KMS signing keys; the private half never leaves
// KMS. One key per env (ENV_DEV, compile-time) per product (runtime, via
// globalHwProfile->product - selected the same way as the AWS IoT
// topics/rules) so a compromised signing pipeline for one product can't
// forge images for the other.

#ifndef ENV_DEV
// Production keys.
constexpr const char* OTA_SIGNING_PUBLIC_KEY_PEM_HOME =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEFtOoSAhpqnoaVbqMTTGsc3t0nMaMp0raYmcJId22\n"
"bzF37RlPSgXIqXRlwUhxhRWJzwVHhouE/hqUWdL3rmdwMg==\n"
"-----END PUBLIC KEY-----\n";
constexpr const char* OTA_SIGNING_PUBLIC_KEY_PEM_HOMEPRO =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEMWFWihKmoYwCCGwuRINmU0/ZVUE2\n"
"1IqjKWKBnBMonB3MuaGxphswkYvqsUeTL7kErKeviD7Lz+DkBCztseIjGQ==\n"
"-----END PUBLIC KEY-----\n";
#else
// Dev/test keys - dev builds only, never reach a vendor device.
constexpr const char* OTA_SIGNING_PUBLIC_KEY_PEM_HOME =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEL30m5KjXuHbjc7Q36kt023IgGid7\n"
"XH1V0oCPXF2ebIUSY+Pm/tIWXEhVA08SE7ROIwHFWdonsXY0lb3BgnOWww==\n"
"-----END PUBLIC KEY-----\n";
constexpr const char* OTA_SIGNING_PUBLIC_KEY_PEM_HOMEPRO =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE9QFnlcef4kbXsd1UdRtTese8fDA9\n"
"ran770WFkjVWuHFfCiFgazlMMBs5EEJVyRnAfx0Tf7cV49/A8LAaAVLNFw==\n"
"-----END PUBLIC KEY-----\n";
#endif
