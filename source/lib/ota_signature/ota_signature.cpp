// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "ota_signature.h"
#include <cstring>

namespace OtaSignature {

static int decodeBase64Char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Standard (non-URL-safe) base64 decode, '=' padding required. Rejects anything
// that isn't a well-formed multiple-of-4 base64 string, or that would overflow
// outCapacity - both are treated as "not a valid signature", not a partial result.
static bool base64Decode(const char *input, size_t inputLen, uint8_t *out, size_t outCapacity, size_t &outLen) {
    outLen = 0;
    if (inputLen == 0 || inputLen % 4 != 0) return false;

    size_t effectiveLen = inputLen;
    if (input[effectiveLen - 1] == '=') effectiveLen--;
    if (effectiveLen > 0 && input[effectiveLen - 1] == '=') effectiveLen--;

    uint32_t buffer = 0;
    int bitsCollected = 0;
    for (size_t i = 0; i < effectiveLen; i++) {
        int value = decodeBase64Char(input[i]);
        if (value < 0) return false;
        buffer = (buffer << 6) | (uint32_t)value;
        bitsCollected += 6;
        if (bitsCollected >= 8) {
            bitsCollected -= 8;
            if (outLen >= outCapacity) return false;
            out[outLen++] = (uint8_t)((buffer >> bitsCollected) & 0xFF);
        }
    }
    return true;
}

// Reads one DER length field at data[pos], advancing pos past it. Only short-form
// (<0x80) and single-byte long-form (0x81) are accepted - more than enough for a
// signature capped at MAX_DER_SIGNATURE_LEN, and anything else is rejected as
// malformed rather than parsed further.
static bool parseDerLength(const uint8_t *data, size_t dataLen, size_t &pos, size_t &length) {
    if (pos >= dataLen) return false;
    uint8_t first = data[pos++];
    if (first < 0x80) {
        length = first;
        return true;
    }
    if (first == 0x81) {
        if (pos >= dataLen) return false;
        length = data[pos++];
        return true;
    }
    return false;
}

// Consumes one "02 <len> <content>" INTEGER TLV at data[pos], advancing pos past
// it. Rejects a zero-length or out-of-bounds content, same as a malformed tag.
static bool parseDerInteger(const uint8_t *data, size_t dataLen, size_t &pos) {
    if (pos >= dataLen || data[pos++] != 0x02) return false;
    size_t contentLen;
    if (!parseDerLength(data, dataLen, pos, contentLen)) return false;
    if (contentLen == 0 || pos + contentLen > dataLen) return false;
    pos += contentLen;
    return true;
}

static bool validateDerEcdsaSignatureShape(const uint8_t *der, size_t len) {
    if (!der || len < 8 || len > MAX_DER_SIGNATURE_LEN) return false;

    size_t pos = 0;
    if (der[pos++] != 0x30) return false;  // SEQUENCE tag

    size_t seqLen;
    if (!parseDerLength(der, len, pos, seqLen)) return false;
    if (pos + seqLen != len) return false;  // outer SEQUENCE must span exactly the decoded buffer

    if (!parseDerInteger(der, len, pos)) return false;  // INTEGER r
    if (!parseDerInteger(der, len, pos)) return false;  // INTEGER s

    return pos == len;  // no trailing garbage after the two integers
}

bool decodeAndValidate(const char *base64Signature, uint8_t *outDer, size_t outCapacity, size_t &outLen) {
    outLen = 0;
    if (!base64Signature || base64Signature[0] == '\0') return false;

    size_t inputLen = strlen(base64Signature);
    if (inputLen > MAX_BASE64_SIGNATURE_INPUT_LEN) return false;

    size_t decodedLen = 0;
    if (!base64Decode(base64Signature, inputLen, outDer, outCapacity, decodedLen)) return false;

    if (!validateDerEcdsaSignatureShape(outDer, decodedLen)) return false;

    outLen = decodedLen;
    return true;
}

}  // namespace OtaSignature
