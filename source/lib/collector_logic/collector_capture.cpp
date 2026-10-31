// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

// Capture extraction, encoding chains, the session expiry rule and the
// capture echo scrub (spec "Captures", "Session handling with one re-login").
// No allocation: the JSON key lookup is a bounded scanner over the top level.

#include <cstring>

#include "collector_logic.h"
#include "gateway_template.h"

namespace CollectorLogic {

namespace {

constexpr size_t CAPTURE_KEY_BUFFER = 65;

// First occurrence of `needle` in `hay`, or `hayLength` when absent.
size_t find(const uint8_t *hay, size_t hayLength, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0 || n > hayLength) return hayLength;
    for (size_t i = 0; i + n <= hayLength; i++) {
        if (hay[i] == static_cast<uint8_t>(needle[0]) && memcmp(hay + i, needle, n) == 0) return i;
    }
    return hayLength;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool sameNameIgnoreCase(const char *a, size_t aLength, const char *b) {
    size_t bLength = strlen(b);
    if (aLength != bLength) return false;
    for (size_t i = 0; i < aLength; i++) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

bool copyValue(const uint8_t *value, size_t length, uint8_t *out, size_t cap, size_t *outLength) {
    if (length < 1 || length > CAPTURE_VALUE_MAX || length > cap) return false;
    memcpy(out, value, length);
    *outLength = length;
    return true;
}

bool fromHeader(const Capture &capture, const char *headers, size_t length, uint8_t *out, size_t cap, size_t *outLength) {
    if (headers == nullptr || capture.header == nullptr) return false;
    size_t pos = 0;
    bool statusLine = true;
    while (pos < length) {
        size_t end = pos;
        while (end < length && headers[end] != '\n') end++;
        size_t lineEnd = end;
        while (lineEnd > pos && (headers[lineEnd - 1] == '\r' || headers[lineEnd - 1] == ' ' || headers[lineEnd - 1] == '\t')) lineEnd--;
        size_t next = end + 1;
        if (statusLine) {
            statusLine = false;
            pos = next;
            continue;
        }
        if (lineEnd == pos) break; // blank line ends the header block
        const char *line = headers + pos;
        size_t lineLength = lineEnd - pos;
        const char *colon = static_cast<const char *>(memchr(line, ':', lineLength));
        pos = next;
        if (colon == nullptr || !sameNameIgnoreCase(line, static_cast<size_t>(colon - line), capture.header)) continue;
        const uint8_t *value = reinterpret_cast<const uint8_t *>(colon + 1);
        size_t valueLength = lineLength - static_cast<size_t>(colon - line) - 1;
        while (valueLength > 0 && (*value == ' ' || *value == '\t')) {
            value++;
            valueLength--;
        }
        if (capture.after != nullptr) {
            size_t at = find(value, valueLength, capture.after);
            if (at == valueLength) continue; // the next line with this name may hold it
            size_t skip = at + strlen(capture.after);
            value += skip;
            valueLength -= skip;
        }
        if (capture.before != nullptr) valueLength = find(value, valueLength, capture.before);
        return copyValue(value, valueLength, out, cap, outLength);
    }
    return false;
}

bool fromDelimiters(const Capture &capture, const uint8_t *body, size_t length, uint8_t *out, size_t cap, size_t *outLength) {
    if (body == nullptr || capture.after == nullptr || capture.before == nullptr) return false;
    size_t at = find(body, length, capture.after);
    if (at == length) return false;
    size_t start = at + strlen(capture.after);
    size_t span = find(body + start, length - start, capture.before);
    if (span == length - start) return false;
    return copyValue(body + start, span, out, cap, outLength);
}

bool isWs(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

size_t skipWs(const uint8_t *s, size_t length, size_t i) {
    while (i < length && isWs(s[i])) i++;
    return i;
}

int hexValue(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool readHex4(const uint8_t *s, size_t length, size_t i, uint32_t *out) {
    if (i + 4 > length) return false;
    uint32_t v = 0;
    for (size_t k = 0; k < 4; k++) {
        int h = hexValue(s[i + k]);
        if (h < 0) return false;
        v = (v << 4) | static_cast<uint32_t>(h);
    }
    *out = v;
    return true;
}

// Decodes the JSON string starting at s[*i] == '"' into `out` (when not null).
// `*overflow` is set when it did not fit; the scan still ends past the string.
bool readString(const uint8_t *s, size_t length, size_t *i, uint8_t *out, size_t cap, size_t *outLength, bool *overflow) {
    size_t p = *i + 1;
    size_t used = 0;
    *overflow = false;
    auto put = [&](uint32_t byte) {
        if (out == nullptr) return;
        if (used >= cap) {
            *overflow = true;
            return;
        }
        out[used++] = static_cast<uint8_t>(byte);
    };
    while (p < length) {
        uint8_t c = s[p];
        if (c == '"') {
            *i = p + 1;
            *outLength = used;
            return true;
        }
        if (c < 0x20) return false;
        if (c != '\\') {
            put(c);
            p++;
            continue;
        }
        if (p + 1 >= length) return false;
        uint8_t e = s[p + 1];
        p += 2;
        switch (e) {
            case '"': put('"'); break;
            case '\\': put('\\'); break;
            case '/': put('/'); break;
            case 'b': put('\b'); break;
            case 'f': put('\f'); break;
            case 'n': put('\n'); break;
            case 'r': put('\r'); break;
            case 't': put('\t'); break;
            case 'u': {
                uint32_t cp = 0;
                if (!readHex4(s, length, p, &cp)) return false;
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t low = 0;
                    if (p + 6 > length || s[p] != '\\' || s[p + 1] != 'u' || !readHex4(s, length, p + 2, &low) || low < 0xDC00 || low > 0xDFFF)
                        return false;
                    p += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;
                }
                if (cp < 0x80) {
                    put(cp);
                } else if (cp < 0x800) {
                    put(0xC0 | (cp >> 6));
                    put(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    put(0xE0 | (cp >> 12));
                    put(0x80 | ((cp >> 6) & 0x3F));
                    put(0x80 | (cp & 0x3F));
                } else {
                    put(0xF0 | (cp >> 18));
                    put(0x80 | ((cp >> 12) & 0x3F));
                    put(0x80 | ((cp >> 6) & 0x3F));
                    put(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: return false;
        }
    }
    return false;
}

bool isNumberChar(uint8_t c) { return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E'; }

bool skipString(const uint8_t *s, size_t length, size_t *i) {
    size_t ignored = 0;
    bool overflow = false;
    return readString(s, length, i, nullptr, 0, &ignored, &overflow);
}

// Skips one JSON value starting at s[*i]; nested containers by a depth counter.
bool skipValue(const uint8_t *s, size_t length, size_t *i) {
    size_t p = *i;
    if (p >= length) return false;
    if (s[p] == '"') {
        if (!skipString(s, length, &p)) return false;
        *i = p;
        return true;
    }
    if (s[p] == '{' || s[p] == '[') {
        size_t depth = 0;
        while (p < length) {
            uint8_t c = s[p];
            if (c == '"') {
                if (!skipString(s, length, &p)) return false;
                continue;
            }
            if (c == '{' || c == '[') depth++;
            if (c == '}' || c == ']') {
                if (depth == 0) return false;
                depth--;
                if (depth == 0) {
                    *i = p + 1;
                    return true;
                }
            }
            p++;
        }
        return false;
    }
    size_t start = p;
    while (p < length && !isWs(s[p]) && s[p] != ',' && s[p] != '}' && s[p] != ']') p++;
    if (p == start) return false;
    *i = p;
    return true;
}

bool fromJsonKey(const Capture &capture, const uint8_t *body, size_t length, uint8_t *out, size_t cap, size_t *outLength) {
    if (body == nullptr) return false;
    size_t i = skipWs(body, length, 0);
    if (i >= length || body[i] != '{') return false;
    i++;
    size_t keyLength = strlen(capture.key);
    while (i < length) {
        i = skipWs(body, length, i);
        if (i >= length || body[i] != '"') return false;
        uint8_t key[CAPTURE_KEY_BUFFER];
        size_t decoded = 0;
        bool overflow = false;
        if (!readString(body, length, &i, key, sizeof(key), &decoded, &overflow)) return false;
        bool match = !overflow && decoded == keyLength && memcmp(key, capture.key, keyLength) == 0;
        i = skipWs(body, length, i);
        if (i >= length || body[i] != ':') return false;
        i = skipWs(body, length, i + 1);
        if (i >= length) return false;
        if (match) {
            if (body[i] == '"') {
                size_t limit = cap < CAPTURE_VALUE_MAX ? cap : CAPTURE_VALUE_MAX;
                if (!readString(body, length, &i, out, limit, outLength, &overflow) || overflow) return false;
                return *outLength >= 1;
            }
            if (body[i] == '-' || (body[i] >= '0' && body[i] <= '9')) {
                size_t start = i;
                while (i < length && isNumberChar(body[i])) i++;
                return copyValue(body + start, i - start, out, cap, outLength);
            }
            return false; // true, false, null, object or array
        }
        if (!skipValue(body, length, &i)) return false;
        i = skipWs(body, length, i);
        if (i >= length || body[i] != ',') return false;
        i++;
    }
    return false;
}

} // namespace

CaptureStatus extractCapture(const Capture &capture, const char *headers, size_t headersLength, const uint8_t *body, size_t bodyLength,
                             uint8_t *out, size_t cap, size_t *length) {
    *length = 0;
    bool ok = false;
    if (out != nullptr && cap > 0) {
        if (capture.fromHeader) {
            ok = fromHeader(capture, headers, headersLength, out, cap, length);
        } else if (capture.key != nullptr) {
            ok = fromJsonKey(capture, body, bodyLength, out, cap, length);
        } else {
            ok = fromDelimiters(capture, body, bodyLength, out, cap, length);
        }
    }
    if (ok && *length >= 1 && *length <= CAPTURE_VALUE_MAX) return CaptureStatus::OK;
    if (out != nullptr && cap > 0) memset(out, 0, cap < CAPTURE_VALUE_MAX ? cap : CAPTURE_VALUE_MAX); // no partial value kept
    *length = 0;
    return CaptureStatus::MISSING;
}

size_t applyChain(const EncodingChain &chain, const uint8_t *value, size_t length, uint8_t *out, size_t cap, uint8_t *scratch, size_t scratchCap) {
    if (chain.count == 0) {
        if (length > cap) return 0;
        memcpy(out, value, length);
        return length;
    }
    // Alternate buffers so the last encoding writes into `out`.
    uint8_t *buffers[2] = {out, scratch};
    size_t caps[2] = {cap, scratchCap};
    size_t target = (chain.count % 2 == 1) ? 0 : 1;
    const uint8_t *from = value;
    size_t fromLength = length;
    for (size_t e = 0; e < chain.count && e < 3; e++) {
        bool ok = false;
        size_t produced = 0;
        switch (chain.encodings[e]) {
            case Encoding::URL: produced = GatewayLogic::encodeUrl(from, fromLength, buffers[target], caps[target], &ok); break;
            case Encoding::JSON: produced = GatewayLogic::encodeJson(from, fromLength, buffers[target], caps[target], &ok); break;
            case Encoding::B64: produced = GatewayLogic::encodeBase64(from, fromLength, buffers[target], caps[target], &ok); break;
            case Encoding::NONE:
                if (fromLength > caps[target]) return 0;
                memmove(buffers[target], from, fromLength);
                produced = fromLength;
                ok = true;
                break;
        }
        if (!ok) return 0;
        from = buffers[target];
        fromLength = produced;
        target = 1 - target;
    }
    return fromLength;
}

bool isExpired(const Job &job, int protoStatus, const uint8_t *body, size_t bodyLength) {
    if (!job.hasSession) return false;
    for (size_t i = 0; i < job.expiryStatusCount && i < 4; i++) {
        if (protoStatus == job.expiryStatus[i]) return true;
    }
    return job.expiryMarker != nullptr && body != nullptr && find(body, bodyLength, job.expiryMarker) < bodyLength;
}

size_t scrubCaptures(uint8_t *buffer, size_t length, size_t cap, uint8_t *scratch, const SecretsLogic::ScrubForm *forms, size_t formCount,
                     bool *truncated, bool *redacted) {
    return SecretsLogic::scrubAs("cap", buffer, length, cap, scratch, forms, formCount, truncated, redacted);
}

} // namespace CollectorLogic
