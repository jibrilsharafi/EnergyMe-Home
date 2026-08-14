// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure, dependency-free base64 decode + DER shape check for the MQTT/cloud OTA job
// document's `firmware.signature` field. Deliberately has no mbedtls dependency so
// it can be exercised by native host unit tests - the actual cryptographic proof
// (mbedtls_pk_verify against the downloaded firmware's hash) is ESP32-only code in
// Mqtt::_verifyOtaSignature (mqtt.cpp) and is not covered by these host tests; see
// openspec/changes/mqtt-ota-signature-verification/tasks.md 6.3-6.5 for the
// hardware/e2e coverage of that part.

namespace OtaSignature {

// Max size of a DER-encoded ECDSA-P256 signature (SEQUENCE{INTEGER r, INTEGER s}):
// 2 (SEQUENCE tag+len) + 2 * (2 (INTEGER tag+len) + 33 (worst case: leading 0x00
// pad byte + 32-byte coordinate)) = 72, with a small safety margin.
constexpr size_t MAX_DER_SIGNATURE_LEN = 74;

// Longest realistic base64 encoding of a signature this size (next multiple of 4
// at/above ceil(MAX_DER_SIGNATURE_LEN / 3) * 4), with margin for padding.
constexpr size_t MAX_BASE64_SIGNATURE_INPUT_LEN = 104;

// Decodes a base64-encoded DER ECDSA signature from an OTA job document and
// performs minimal structural validation: base64 well-formedness, DER
// SEQUENCE{INTEGER, INTEGER} shape, and that the whole decoded buffer is consumed
// exactly (no trailing garbage). This is NOT cryptographic verification - it only
// rejects an obviously missing/malformed/oversized field before a firmware
// download is started (see the spec's "Signature field absent or malformed"
// scenario).
//
// Returns true and fills outDer[0..outLen) on success. Returns false (outLen left
// at 0) if base64Signature is null/empty, longer than
// MAX_BASE64_SIGNATURE_INPUT_LEN, fails to base64-decode, decodes to more bytes
// than outCapacity, or doesn't have the shape of a DER ECDSA signature.
bool decodeAndValidate(const char *base64Signature, uint8_t *outDer, size_t outCapacity, size_t &outLen);

}  // namespace OtaSignature
