// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "gateway_template.h"

#include <cstring>

namespace GatewayLogic {

namespace {

enum class Encoding { URL, JSON, B64 };

bool isNameChar(char c) { return (c >= 'a' && c <= 'z') || c == '_'; }

bool isArgChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
}

bool isUnreserved(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
}

const char HEX[] = "0123456789ABCDEF";
const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct Placeholder {
    char name[TEMPLATE_NAME_MAX + 1];
    char arg[TEMPLATE_ARG_MAX + 1];
    Encoding encodings[TEMPLATE_MAX_ENCODINGS];
    size_t encodingCount;
    size_t length; // bytes consumed from '$' to '}' inclusive
};

bool matchWord(const char *p, const char *end, const char *word) {
    size_t n = strlen(word);
    return static_cast<size_t>(end - p) >= n && memcmp(p, word, n) == 0;
}

// Parses `${name[:arg][|enc]...}` starting at `p` ('$').
bool parsePlaceholder(const char *p, const char *end, Placeholder *out) {
    const char *s = p + 2;
    size_t n = 0;
    while (s < end && isNameChar(*s)) {
        if (n >= TEMPLATE_NAME_MAX) return false;
        out->name[n++] = *s++;
    }
    if (n == 0) return false;
    out->name[n] = '\0';
    out->arg[0] = '\0';
    if (s < end && *s == ':') {
        s++;
        n = 0;
        while (s < end && isArgChar(*s)) {
            if (n >= TEMPLATE_ARG_MAX) return false;
            out->arg[n++] = *s++;
        }
        if (n == 0) return false;
        out->arg[n] = '\0';
    }
    out->encodingCount = 0;
    while (s < end && *s == '|') {
        if (out->encodingCount >= TEMPLATE_MAX_ENCODINGS) return false;
        s++;
        Encoding encoding;
        if (matchWord(s, end, "url")) {
            encoding = Encoding::URL;
            s += 3;
        } else if (matchWord(s, end, "json")) {
            encoding = Encoding::JSON;
            s += 4;
        } else if (matchWord(s, end, "b64")) {
            encoding = Encoding::B64;
            s += 3;
        } else {
            return false;
        }
        out->encodings[out->encodingCount++] = encoding;
    }
    if (s >= end || *s != '}') return false;
    out->length = static_cast<size_t>(s - p) + 1;
    return true;
}

bool append(uint8_t *out, size_t cap, size_t *used, const void *data, size_t length) {
    if (length > cap - *used) return false;
    memcpy(out + *used, data, length);
    *used += length;
    return true;
}

} // namespace

size_t encodeUrl(const uint8_t *in, size_t length, uint8_t *out, size_t cap, bool *ok) {
    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
        if (isUnreserved(in[i])) {
            if (used + 1 > cap) return *ok = false, 0;
            out[used++] = in[i];
        } else {
            if (used + 3 > cap) return *ok = false, 0;
            out[used++] = '%';
            out[used++] = static_cast<uint8_t>(HEX[in[i] >> 4]);
            out[used++] = static_cast<uint8_t>(HEX[in[i] & 0x0F]);
        }
    }
    *ok = true;
    return used;
}

size_t encodeJson(const uint8_t *in, size_t length, uint8_t *out, size_t cap, bool *ok) {
    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
        uint8_t c = in[i];
        char escaped[7];
        size_t n = 0;
        switch (c) {
            case '"': memcpy(escaped, "\\\"", n = 2); break;
            case '\\': memcpy(escaped, "\\\\", n = 2); break;
            case '\b': memcpy(escaped, "\\b", n = 2); break;
            case '\f': memcpy(escaped, "\\f", n = 2); break;
            case '\n': memcpy(escaped, "\\n", n = 2); break;
            case '\r': memcpy(escaped, "\\r", n = 2); break;
            case '\t': memcpy(escaped, "\\t", n = 2); break;
            default:
                if (c < 0x20) {
                    memcpy(escaped, "\\u00", 4);
                    escaped[4] = HEX[c >> 4];
                    escaped[5] = HEX[c & 0x0F];
                    n = 6;
                } else {
                    escaped[0] = static_cast<char>(c);
                    n = 1;
                }
        }
        if (used + n > cap) return *ok = false, 0;
        memcpy(out + used, escaped, n);
        used += n;
    }
    *ok = true;
    return used;
}

size_t encodeBase64(const uint8_t *in, size_t length, uint8_t *out, size_t cap, bool *ok) {
    size_t needed = (length + 2) / 3 * 4;
    if (needed > cap) return *ok = false, 0;
    size_t used = 0;
    for (size_t i = 0; i < length; i += 3) {
        uint32_t v = static_cast<uint32_t>(in[i]) << 16;
        if (i + 1 < length) v |= static_cast<uint32_t>(in[i + 1]) << 8;
        if (i + 2 < length) v |= in[i + 2];
        out[used++] = static_cast<uint8_t>(B64[(v >> 18) & 0x3F]);
        out[used++] = static_cast<uint8_t>(B64[(v >> 12) & 0x3F]);
        out[used++] = static_cast<uint8_t>(i + 1 < length ? B64[(v >> 6) & 0x3F] : '=');
        out[used++] = static_cast<uint8_t>(i + 2 < length ? B64[v & 0x3F] : '=');
    }
    *ok = true;
    return used;
}

bool hasPlaceholder(const char *text, size_t length) {
    for (size_t i = 0; i + 1 < length; i++) {
        if (text[i] == '$' && text[i + 1] == '{') return true;
    }
    return false;
}

ExpandStatus expandTemplate(const char *tmpl, size_t length, const Resolver *chain, size_t chainLength, const ResolveContext &request,
                            uint8_t *out, size_t cap, uint8_t *scratch, size_t scratchCap, ExpandResult *result) {
    result->length = 0;
    const char *p = tmpl;
    const char *end = tmpl + length;
    size_t used = 0;
    size_t half = scratchCap / 2;
    uint8_t *buffers[2] = {scratch, scratch + half};

    while (p < end) {
        if (*p != '$') {
            const char *next = static_cast<const char *>(memchr(p, '$', static_cast<size_t>(end - p)));
            size_t run = next != nullptr ? static_cast<size_t>(next - p) : static_cast<size_t>(end - p);
            if (!append(out, cap, &used, p, run)) return ExpandStatus::TOO_LONG;
            p += run;
            continue;
        }
        if (end - p >= 3 && p[1] == '$' && p[2] == '{') { // $${ -> literal ${
            if (!append(out, cap, &used, "${", 2)) return ExpandStatus::TOO_LONG;
            p += 3;
            continue;
        }
        if (end - p < 2 || p[1] != '{') { // a lone '$' is literal
            if (!append(out, cap, &used, p, 1)) return ExpandStatus::TOO_LONG;
            p++;
            continue;
        }

        Placeholder placeholder{};
        if (!parsePlaceholder(p, end, &placeholder) || chain == nullptr || half == 0) return ExpandStatus::BAD_TEMPLATE;

        ResolveStatus status = ResolveStatus::UNKNOWN;
        ResolveOutput value{};
        for (size_t i = 0; i < chainLength && status == ResolveStatus::UNKNOWN; i++) {
            if (chain[i].resolve == nullptr) continue;
            value = ResolveOutput{};
            value.value = buffers[0];
            value.cap = half;
            status = chain[i].resolve(chain[i].context, request, placeholder.name, placeholder.arg, &value);
        }
        if (status == ResolveStatus::UNKNOWN) return ExpandStatus::BAD_TEMPLATE;
        if (status == ResolveStatus::FAILED) {
            result->reasonCode = value.reasonCode;
            memcpy(result->reasonDetail, value.reasonDetail, sizeof(result->reasonDetail));
            result->reasonDetail[sizeof(result->reasonDetail) - 1] = '\0';
            return ExpandStatus::RESOLVER_FAILED;
        }
        if (value.length > half) return ExpandStatus::TOO_LONG;
        result->sensitive = result->sensitive || value.sensitive;
        if (value.requireMac) {
            if (result->requireMac && memcmp(result->mac, value.mac, 6) != 0) result->macConflict = true;
            if (!result->requireMac) {
                memcpy(result->mac, value.mac, 6);
                result->macReasonCode = value.macReasonCode;
            }
            result->requireMac = true;
        }

        size_t current = 0, currentLength = value.length;
        for (size_t e = 0; e < placeholder.encodingCount; e++) {
            bool ok = false;
            uint8_t *from = buffers[current], *to = buffers[1 - current];
            switch (placeholder.encodings[e]) {
                case Encoding::URL: currentLength = encodeUrl(from, currentLength, to, half, &ok); break;
                case Encoding::JSON: currentLength = encodeJson(from, currentLength, to, half, &ok); break;
                case Encoding::B64: currentLength = encodeBase64(from, currentLength, to, half, &ok); break;
            }
            if (!ok) return ExpandStatus::TOO_LONG;
            current = 1 - current;
        }
        if (!append(out, cap, &used, buffers[current], currentLength)) return ExpandStatus::TOO_LONG;
        p += placeholder.length;
    }
    result->length = used;
    return ExpandStatus::OK;
}

} // namespace GatewayLogic
