// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "secrets_logic.h"

#include <cstdio>
#include <cstring>

namespace SecretsLogic {

namespace {

void putLe(uint8_t *p, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; i++) p[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint64_t getLe(const uint8_t *p, size_t bytes) {
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; i++) value |= static_cast<uint64_t>(p[i]) << (8 * i);
    return value;
}

bool isZeroMac(const uint8_t mac[6]) {
    for (size_t i = 0; i < 6; i++) {
        if (mac[i] != 0) return false;
    }
    return true;
}

// Length of the strict UTF-8 sequence at `p`, 0 when invalid.
size_t utf8Sequence(const uint8_t *p, size_t remaining) {
    uint8_t c = p[0];
    if (c < 0x80) return 1;
    size_t length;
    uint32_t min;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) {
        length = 2;
        min = 0x80;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        length = 3;
        min = 0x800;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        length = 4;
        min = 0x10000;
        cp = c & 0x07;
    } else {
        return 0;
    }
    if (length > remaining) return 0;
    for (size_t i = 1; i < length; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return length;
}

} // namespace

bool isValidName(const char *name) {
    if (name == nullptr || !(name[0] >= 'a' && name[0] <= 'z')) return false;
    size_t length = 0;
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (++length > SECRET_NAME_MAX) return false;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

bool isValidValue(const uint8_t *value, size_t length) {
    if (value == nullptr || length < 1 || length > VALUE_MAX) return false;
    for (size_t i = 0; i < length;) {
        if (value[i] == 0 || value[i] == '\r' || value[i] == '\n') return false;
        size_t n = utf8Sequence(value + i, length - i);
        if (n == 0) return false;
        i += n;
    }
    return true;
}

size_t encodeSlot(const SlotMeta &meta, const uint8_t *value, size_t length, uint8_t *out, size_t cap) {
    size_t nameLength = strlen(meta.name);
    size_t size = SLOT_HEADER_BYTES + length;
    if (!isValidName(meta.name) || !isValidValue(value, length) || out == nullptr || cap < size) return 0;
    memset(out, 0, SLOT_HEADER_BYTES);
    out[0] = SLOT_VERSION;
    out[1] = static_cast<uint8_t>(nameLength);
    memcpy(out + 2, meta.name, nameLength);
    memcpy(out + 26, meta.ip, 4);
    putLe(out + 30, meta.port, 2);
    if (meta.hasMac) memcpy(out + 32, meta.mac, 6);
    putLe(out + 38, meta.setAt, 8);
    putLe(out + 46, length, 2);
    memcpy(out + SLOT_HEADER_BYTES, value, length);
    return size;
}

bool decodeSlot(const uint8_t *blob, size_t size, SlotMeta *meta, const uint8_t **value, size_t *length) {
    if (blob == nullptr || size < SLOT_HEADER_BYTES || blob[0] != SLOT_VERSION) return false;
    size_t nameLength = blob[1];
    size_t valueLength = static_cast<size_t>(getLe(blob + 46, 2));
    if (nameLength < 1 || nameLength > SECRET_NAME_MAX || valueLength < 1 || valueLength > VALUE_MAX || size != SLOT_HEADER_BYTES + valueLength) return false;
    for (size_t i = 0; i < nameLength; i++) {
        if (blob[2 + i] == 0) return false; // a NUL inside name_len would hide the rest from the name rule
    }
    for (size_t i = nameLength; i < SECRET_NAME_MAX; i++) {
        if (blob[2 + i] != 0) return false;
    }
    SlotMeta decoded{};
    memcpy(decoded.name, blob + 2, nameLength);
    decoded.name[nameLength] = '\0';
    memcpy(decoded.ip, blob + 26, 4);
    decoded.port = static_cast<uint16_t>(getLe(blob + 30, 2));
    memcpy(decoded.mac, blob + 32, 6);
    decoded.hasMac = !isZeroMac(decoded.mac);
    decoded.setAt = getLe(blob + 38, 8);
    if (!isValidName(decoded.name) || decoded.port == 0 || !GatewayLogic::isPrivateIpv4(decoded.ip)) return false;
    if (decoded.hasMac && (decoded.mac[0] & 0x01) != 0) return false; // multicast and broadcast
    if (!isValidValue(blob + SLOT_HEADER_BYTES, valueLength)) return false;
    *meta = decoded;
    if (value != nullptr) *value = blob + SLOT_HEADER_BYTES;
    if (length != nullptr) *length = valueLength;
    return true;
}

size_t buildAad(char *out, size_t cap, const char *deviceId, const char *name, const uint8_t ip[4], uint16_t port, const uint8_t *mac,
                uint64_t issuedAt) {
    char macText[GatewayLogic::MAC_TEXT_BUFFER] = "";
    if (mac != nullptr) GatewayLogic::formatMac(mac, macText, sizeof(macText));
    int n = snprintf(out, cap, "energyme-secret-v1|%s|%s|%u.%u.%u.%u:%u|%s|%llu", deviceId, name, ip[0], ip[1], ip[2], ip[3], port, macText,
                     static_cast<unsigned long long>(issuedAt));
    return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

bool parseIssuedAt(const char *text, uint64_t *out) {
    if (text == nullptr || text[0] < '1' || text[0] > '9') return false;
    uint64_t value = 0;
    size_t digits = 0;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9' || ++digits > 19) return false;
        value = value * 10 + static_cast<uint64_t>(*p - '0');
    }
    *out = value;
    return true;
}

bool unpadPlaintext(const uint8_t *plaintext, size_t size, const uint8_t **value, size_t *length) {
    if (plaintext == nullptr || size != PLAINTEXT_BYTES) return false;
    size_t valueLength = static_cast<size_t>(getLe(plaintext, 2));
    if (valueLength < 1 || valueLength > VALUE_MAX) return false;
    uint8_t padding = 0;
    for (size_t i = 2 + valueLength; i < PLAINTEXT_BYTES; i++) padding |= plaintext[i];
    if (padding != 0) return false;
    *value = plaintext + 2;
    *length = valueLength;
    return true;
}

uint64_t applyCeiling(bool clockSynced, uint64_t now, uint64_t hwm, bool *lowered) {
    *lowered = clockSynced && hwm > now + WINDOW_FUTURE_S;
    return *lowered ? now + WINDOW_FUTURE_S : hwm;
}

SetVerdict decideSet(bool clockSynced, uint64_t now, uint64_t hwm, uint64_t issuedAt, bool sameSlot) {
    if (!clockSynced) return SetVerdict::CLOCK_NOT_SYNCED;
    if (issuedAt + WINDOW_PAST_S < now) return SetVerdict::STALE;
    if (issuedAt > now + WINDOW_FUTURE_S) return SetVerdict::CLOCK_SKEW;
    if (sameSlot) return SetVerdict::ALREADY_APPLIED;
    if (issuedAt <= hwm) return SetVerdict::STALE;
    return SetVerdict::APPLY;
}

uint64_t hwmAfterDelete(bool clockSynced, uint64_t now, uint64_t hwm) { return clockSynced && now > hwm ? now : hwm; }

uint64_t localSetAt(bool clockSynced, uint64_t now, uint64_t hwm) { return clockSynced && now > hwm + 1 ? now : hwm + 1; }

Decision decide(const GatewayLogic::ResolveContext &context, const char *arg, const SlotMeta *slots, const bool *used, size_t count,
                size_t placeholders) {
    Decision d{-1, nullptr, nullptr, false};
    if (!isValidName(arg) || placeholders >= PLACEHOLDERS_MAX) {
        d.reasonCode = "BAD_TEMPLATE";
        d.reasonDetail = placeholders >= PLACEHOLDERS_MAX ? "At most 8 secret placeholders per request" : "Invalid secret name";
        return d;
    }
    if (context.module != GatewayLogic::Module::HTTP && context.module != GatewayLogic::Module::HTTPS) {
        d.reasonCode = "SECRET_MODULE_NOT_ALLOWED";
        d.reasonDetail = "Secrets expand only into http and https requests";
        return d;
    }
    int slot = -1;
    for (size_t i = 0; i < count && slot < 0; i++) {
        if (used[i] && strcmp(slots[i].name, arg) == 0) slot = static_cast<int>(i);
    }
    if (slot < 0) {
        d.reasonCode = "SECRET_NOT_FOUND";
        d.reasonDetail = "No secret with this name";
        return d;
    }
    const SlotMeta &meta = slots[slot];
    d.reasonCode = "SECRET_TARGET_MISMATCH";
    if (memcmp(meta.ip, context.ip, 4) != 0 || meta.port != context.port) {
        d.reasonDetail = "The secret is bound to another target";
        return d;
    }
    if (context.onLink && !meta.hasMac) {
        d.reasonDetail = "MAC_REQUIRED";
        return d;
    }
    if (meta.hasMac && !context.onLink) {
        d.reasonDetail = "TARGET_MAC_CHANGED";
        return d;
    }
    d.reasonCode = nullptr;
    d.slot = slot;
    d.requireMac = meta.hasMac;
    return d;
}

size_t scrub(uint8_t *buffer, size_t length, size_t cap, uint8_t *scratch, const ScrubForm *forms, size_t formCount, bool *truncated,
             bool *redacted) {
    size_t out = 0;
    size_t i = 0;
    bool cut = false;
    while (i < length) {
        const ScrubForm *match = nullptr;
        for (size_t f = 0; f < formCount; f++) {
            const ScrubForm &form = forms[f];
            if (form.length < SCRUB_MIN_BYTES || form.length > length - i) continue;
            if ((match == nullptr || form.length > match->length) && memcmp(buffer + i, form.bytes, form.length) == 0) match = &form;
        }
        if (match == nullptr) {
            if (out >= cap) {
                cut = true;
                break;
            }
            scratch[out++] = buffer[i++];
            continue;
        }
        char placeholder[PLACEHOLDER_MAX + 1];
        int n = snprintf(placeholder, sizeof(placeholder), "${secret:%s}", match->name);
        size_t placeholderLength = n > 0 ? static_cast<size_t>(n) : 0;
        *redacted = true;
        if (placeholderLength == 0 || placeholderLength >= sizeof(placeholder) || out + placeholderLength > cap) {
            cut = true; // the placeholder would straddle the cap: dropped whole
            break;
        }
        memcpy(scratch + out, placeholder, placeholderLength);
        out += placeholderLength;
        i += match->length;
    }
    if (cut) *truncated = true;

    // A cut reply may end inside a value: drop the longest trailing prefix of any form.
    if (*truncated) {
        size_t drop = 0;
        for (size_t f = 0; f < formCount; f++) {
            const ScrubForm &form = forms[f];
            if (form.length < SCRUB_MIN_BYTES) continue;
            for (size_t p = form.length - 1; p > drop; p--) {
                if (p <= out && memcmp(scratch + out - p, form.bytes, p) == 0) {
                    drop = p;
                    break;
                }
            }
        }
        if (drop > 0) {
            out -= drop;
            *redacted = true;
        }
    }
    memcpy(buffer, scratch, out);
    return out;
}

} // namespace SecretsLogic
