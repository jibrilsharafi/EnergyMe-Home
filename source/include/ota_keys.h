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
// DEV/TEST KEY - DO NOT SHIP TO VENDOR DEVICES.
// This is a locally-generated P-256 keypair for bench testing only. The matching
// private key lives outside this repo. Before any vendor/production release, this
// constant MUST be replaced with the public key exported from the AWS KMS CMK
// provisioned per tasks.md 1.1/1.2 (cross-repo, energyme-infra), and firmware for
// that release must be signed via the CI KMS signing step (tasks.md 1.3/1.4), not
// with the dev private key used to sign test builds against this key.
constexpr const char* OTA_SIGNING_PUBLIC_KEY_PEM =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEeSjhDGTPxlAWyDgzKZxaiMb2hjgn\n"
"d2/09ovrcJBIgTxhDpgqklq0ZOpaJGrNcSnsr1K/B3s35T15u9UiE/c1Ww==\n"
"-----END PUBLIC KEY-----\n";
