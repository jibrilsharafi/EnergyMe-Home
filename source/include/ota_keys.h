// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

// MQTT/Cloud OTA Signing Public Key
// ==================================
// ECDSA P-256 public key used by Mqtt::_verifyOtaSignature() to verify firmware
// signatures delivered via the AWS IoT job document's `firmware.signature` field
// (base64 DER `ECDSA-Sig-Value`, SHA-256 digest) before the OTA boot partition is
// switched. Compiled in, not NVS-stored: this key cannot be altered by anyone with
// device/API access short of reflashing new firmware (see
// openspec/changes/mqtt-ota-signature-verification/design.md, Decision 5).
//
// Gated per build environment so dev and prod trust different signing keys while
// exercising the exact same verification mechanism.

#ifdef ENV_PROD
// PRODUCTION key - NOT YET PROVISIONED. Replace with the public key exported from
// the production AWS KMS CMK (`aws kms get-public-key`, DER converted to PEM - see
// openspec tasks.md 1.1/1.2, cross-repo in energyme-infra) BEFORE the first release
// that ships this feature (2.4.0).
//
// The placeholder below is deliberately NOT a valid key: mbedtls_pk_parse_public_key
// fails on it, so a prod build accidentally made before provisioning FAILS CLOSED -
// every MQTT OTA is rejected with `pubkey_parse_error` rather than the build trusting
// a key that never existed. Local web upload remains available as the recovery path.
constexpr const char* OTA_SIGNING_PUBLIC_KEY_PEM =
"REPLACE_WITH_PRODUCTION_KMS_PUBLIC_KEY_PEM";
#else
// DEV/TEST KEY - dev builds only, never reaches a vendor device (those run
// esp32s3-prod / ENV_PROD). Locally-generated P-256 keypair for bench testing;
// the matching private key lives outside this repo. Test builds are signed with
// it manually, mirroring the CI KMS signing step (tasks.md 1.3/1.4) 1:1.
constexpr const char* OTA_SIGNING_PUBLIC_KEY_PEM =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEW7gKUMXauKdDy9ULgsHjHh1wkgJN\n"
"KQhxRFCESqnrnnZiI2OAfWZ/+QMwFh1k61Z/t50C2pd5dr+1y9NbRn78TA==\n"
"-----END PUBLIC KEY-----\n";
#endif
