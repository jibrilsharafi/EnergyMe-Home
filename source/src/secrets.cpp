// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "secrets.h"

#include <AdvancedLogger.h>
#include <Preferences.h>

#include "mbedtls/base64.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/gcm.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"

#include "constants.h"
#include "customtime.h"
#include "factory_keys.h"
#include "gateway.h"
#include "gateway_executor.h"
#include "globals.h"

using namespace SecretsLogic;

namespace Secrets
{
    static constexpr const char* ALG = "RSA-OAEP-256+A256GCM";
    static constexpr size_t ENVELOPE_EK_BYTES = 256;
    static constexpr size_t ENVELOPE_IV_BYTES = 12;
    static constexpr size_t ENVELOPE_TAG_BYTES = 16;
    static constexpr size_t PEM_MAX = 4096;
    static constexpr uint32_t MUTEX_WAIT_MS = 2000;
    static constexpr const char* DECRYPT_FAILED_DETAIL = "The envelope could not be decrypted";
    static constexpr const char* NAME_RULE_DETAIL = "name must match ^[a-z][a-z0-9_-]{0,23}$";
    static constexpr const char* STORE_FULL_DETAIL = "All 16 slots are used";

    static SlotMeta _slots[SLOT_COUNT];
    static bool _used[SLOT_COUNT];
    static uint64_t _hwm = 0;
    static char _kid[65] = "";
    static bool _loaded = false; // a set before the boot load would overwrite an unseen slot

    static SemaphoreHandle_t _mutex() {
        static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
        return mutex;
    }
    static bool _lock() { return _mutex() != nullptr && xSemaphoreTake(_mutex(), pdMS_TO_TICKS(MUTEX_WAIT_MS)) == pdTRUE; }
    static void _unlock() { xSemaphoreGive(_mutex()); }

    static void _slotKey(size_t index, char* key, size_t size) { snprintf(key, size, "s%02u", (unsigned)index); }

    static void _fail(Outcome& outcome, const char* status, const char* code, const char* format = nullptr, ...) {
        outcome.status = status;
        outcome.reasonCode = code;
        outcome.reasonDetail[0] = '\0';
        if (format != nullptr) {
            va_list args;
            va_start(args, format);
            vsnprintf(outcome.reasonDetail, sizeof(outcome.reasonDetail), format, args);
            va_end(args);
        }
    }

    // Locks only a loaded store; `outcome` gets the reason otherwise.
    static bool _lockLoaded(Outcome& outcome) {
        if (!_lock()) {
            _fail(outcome, "FAILED", "NO_MEMORY", "Secret store busy");
            return false;
        }
        if (_loaded) return true;
        _unlock();
        _fail(outcome, "FAILED", "NO_MEMORY", "Secret store not loaded");
        return false;
    }

    static bool _clockSynced() { return CustomTime::isTimeSynched(); }
    static uint64_t _now() { return CustomTime::getUnixTime(); }

    // Store
    // =====

    static bool _persistHwm(Preferences& preferences, uint64_t hwm) { return preferences.putULong64(SECRETS_HWM_KEY, hwm) == sizeof(uint64_t); }

    // Caller holds the lock and an open read-write namespace. False when the lowered
    // mark could not be persisted (the RAM value is lowered anyway).
    static bool _applyCeiling(Preferences& preferences) {
        bool lowered = false;
        uint64_t capped = applyCeiling(_clockSynced(), _now(), _hwm, &lowered);
        if (!lowered) return true;
        LOG_WARNING("Secrets hwm %llu is ahead of the clock, lowered to %llu", (unsigned long long)_hwm, (unsigned long long)capped);
        _hwm = capped;
        return _persistHwm(preferences, _hwm);
    }

    static void _computeKid() {
        char* pem = (char*)ps_calloc(1, PEM_MAX);
        if (pem == nullptr) return;
        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_FACTORY, true)) {
            preferences.getString(FACTORY_KEY_CERT_PEM, pem, PEM_MAX - 1);
            preferences.end();
        }
        mbedtls_x509_crt crt;
        mbedtls_x509_crt_init(&crt);
        uint8_t digest[32];
        if (pem[0] != '\0' && mbedtls_x509_crt_parse(&crt, (const unsigned char*)pem, strlen(pem) + 1) == 0 &&
            mbedtls_sha256(crt.raw.p, crt.raw.len, digest, 0) == 0) {
            GatewayLogic::formatHexDigest(digest, _kid, sizeof(_kid));
        }
        mbedtls_x509_crt_free(&crt);
        free(pem);
    }

    static void _scrubResult(const GatewayExec::Request& request, GatewayExec::Result& result);

    void begin() {
        GatewayExec::setResultScrub(_scrubResult);
        _computeKid();
        uint8_t* blob = (uint8_t*)ps_malloc(SLOT_MAX_BYTES);
        if (blob == nullptr || !_lock()) {
            free(blob);
            LOG_ERROR("Could not load the secret store");
            return;
        }
        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_SECRETS, false)) {
            _hwm = preferences.getULong64(SECRETS_HWM_KEY, 0);
            for (size_t i = 0; i < SLOT_COUNT; i++) {
                char key[4];
                _slotKey(i, key, sizeof(key));
                _used[i] = false;
                if (!preferences.isKey(key)) continue;
                size_t size = preferences.getBytesLength(key);
                if (size > SLOT_MAX_BYTES || preferences.getBytes(key, blob, size) != size || !decodeSlot(blob, size, &_slots[i], nullptr, nullptr)) {
                    LOG_WARNING("Secret slot %s is malformed, erased", key);
                    preferences.remove(key);
                    continue;
                }
                _used[i] = true;
            }
            mbedtls_platform_zeroize(blob, SLOT_MAX_BYTES);
            for (size_t i = 0; i < SLOT_COUNT; i++) {
                for (size_t j = 0; j < SLOT_COUNT && _used[i]; j++) {
                    if (j == i || !_used[j] || strcmp(_slots[i].name, _slots[j].name) != 0) continue;
                    size_t drop = _slots[i].setAt >= _slots[j].setAt ? j : i;
                    char key[4];
                    _slotKey(drop, key, sizeof(key));
                    LOG_WARNING("Secret slot %s duplicates a name, erased", key);
                    preferences.remove(key);
                    _used[drop] = false;
                }
                if (_used[i] && _slots[i].setAt > _hwm) {
                    _hwm = _slots[i].setAt;
                    _persistHwm(preferences, _hwm);
                }
            }
            preferences.end();
            _loaded = true;
        }
        _unlock();
        free(blob);
        size_t count = 0;
        for (bool used : _used) count += used ? 1 : 0;
        LOG_INFO("Secret store ready: %u of %u slots used", (unsigned)count, (unsigned)SLOT_COUNT);
    }

    const char* kid() { return _kid; }

    static int _find(const char* name) {
        for (size_t i = 0; i < SLOT_COUNT; i++) {
            if (_used[i] && strcmp(_slots[i].name, name) == 0) return (int)i;
        }
        return -1;
    }

    static int _free() {
        for (size_t i = 0; i < SLOT_COUNT; i++) {
            if (!_used[i]) return (int)i;
        }
        return -1;
    }

    // Caller holds the lock. Writes one slot blob (rotation is one NVS write) and raises hwm.
    static bool _write(Preferences& preferences, int index, const SlotMeta& meta, const uint8_t* value, size_t length) {
        uint8_t* blob = (uint8_t*)ps_malloc(SLOT_MAX_BYTES);
        if (blob == nullptr) return false;
        size_t size = encodeSlot(meta, value, length, blob, SLOT_MAX_BYTES);
        char key[4];
        _slotKey((size_t)index, key, sizeof(key));
        bool ok = size > 0 && preferences.putBytes(key, blob, size) == size;
        mbedtls_platform_zeroize(blob, SLOT_MAX_BYTES);
        free(blob);
        if (!ok) return false;
        _slots[index] = meta;
        _used[index] = true;
        // The slot is written; a lost hwm bump is recovered at boot from the slot's set_at.
        if (meta.setAt > _hwm) {
            _hwm = meta.setAt;
            if (!_persistHwm(preferences, _hwm)) LOG_WARNING("Could not persist the secrets hwm");
        }
        return true;
    }

    // Target, name and MAC rules shared by the envelope and REST paths.
    static bool _validateMeta(const char* name, const char* ip, int64_t port, const char* mac, SlotMeta* meta, Outcome& outcome) {
        if (!isValidName(name)) {
            _fail(outcome, "REJECTED", "BAD_ARGS", "%s", NAME_RULE_DETAIL);
            return false;
        }
        Gateway::TargetCheck check = Gateway::validateTarget(ip, meta->ip);
        if (check != Gateway::TargetCheck::OK) {
            _fail(outcome, "REJECTED", Gateway::targetCheckReasonCode(check), "%s", Gateway::targetCheckDescription(check));
            return false;
        }
        if (!GatewayLogic::isValidPort(port)) {
            _fail(outcome, "REJECTED", "BAD_ARGS", "port must be 1-65535");
            return false;
        }
        meta->port = (uint16_t)port;
        meta->hasMac = mac != nullptr && mac[0] != '\0';
        if (meta->hasMac && !GatewayLogic::parseMac(mac, meta->mac)) {
            _fail(outcome, "REJECTED", "BAD_ARGS", "mac must be six lowercase hex pairs, unicast and non-zero");
            return false;
        }
        if (!meta->hasMac) memset(meta->mac, 0, sizeof(meta->mac));
        snprintf(meta->name, sizeof(meta->name), "%s", name);
        return true;
    }

    // Envelope
    // ========

    static bool _decodeExact(const char* text, uint8_t* out, size_t expected) {
        if (text == nullptr) return false;
        size_t length = 0;
        size_t textLength = strlen(text);
        if (mbedtls_base64_decode(nullptr, 0, &length, (const unsigned char*)text, textLength) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || length != expected) {
            return false;
        }
        return mbedtls_base64_decode(out, expected, &length, (const unsigned char*)text, textLength) == 0 && length == expected;
    }

    struct Envelope {
        uint8_t ek[ENVELOPE_EK_BYTES];
        uint8_t iv[ENVELOPE_IV_BYTES];
        uint8_t ct[PLAINTEXT_BYTES];
        uint8_t tag[ENVELOPE_TAG_BYTES];
    };

    // RSA-OAEP-SHA256 unwrap of the AES key with the device key, then AES-256-GCM.
    // Every failure is one outcome so the device is no padding or tag oracle.
    enum class Decrypt { OK, FAILED, UNAVAILABLE };

    static Decrypt _decrypt(const Envelope& envelope, const char* aad, uint8_t* plaintext) {
        char* pem = (char*)ps_calloc(1, PEM_MAX);
        mbedtls_pk_context* pk = (mbedtls_pk_context*)ps_malloc(sizeof(mbedtls_pk_context));
        mbedtls_entropy_context* entropy = (mbedtls_entropy_context*)ps_malloc(sizeof(mbedtls_entropy_context));
        mbedtls_ctr_drbg_context* drbg = (mbedtls_ctr_drbg_context*)ps_malloc(sizeof(mbedtls_ctr_drbg_context));
        mbedtls_gcm_context* gcm = (mbedtls_gcm_context*)ps_malloc(sizeof(mbedtls_gcm_context));
        bool ok = pem && pk && entropy && drbg && gcm;
        bool keyReady = false; // past this point every failure is the one SECRET_DECRYPT_FAILED
        uint8_t key[32];
        if (ok) {
            mbedtls_pk_init(pk);
            mbedtls_entropy_init(entropy);
            mbedtls_ctr_drbg_init(drbg);
            mbedtls_gcm_init(gcm);
            Preferences preferences;
            if (preferences.begin(PREFERENCES_NAMESPACE_FACTORY, true)) {
                preferences.getString(FACTORY_KEY_KEY_PEM, pem, PEM_MAX - 1);
                preferences.end();
            }
            static const char PERS[] = "energyme-secret";
            size_t keyLength = 0;
            ok = pem[0] != '\0' && mbedtls_ctr_drbg_seed(drbg, mbedtls_entropy_func, entropy, (const unsigned char*)PERS, sizeof(PERS) - 1) == 0 &&
                 mbedtls_pk_parse_key(pk, (const unsigned char*)pem, strlen(pem) + 1, nullptr, 0, mbedtls_ctr_drbg_random, drbg) == 0 &&
                 mbedtls_pk_get_type(pk) == MBEDTLS_PK_RSA && mbedtls_pk_get_bitlen(pk) == 2048; // `ek` is exactly 256 bytes
            keyReady = ok;
            if (ok) {
                mbedtls_rsa_context* rsa = mbedtls_pk_rsa(*pk);
                // Explicit: without it mbedTLS silently decrypts PKCS#1 v1.5.
                ok = mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256) == 0 &&
                     mbedtls_rsa_rsaes_oaep_decrypt(rsa, mbedtls_ctr_drbg_random, drbg, nullptr, 0, &keyLength, envelope.ek, key, sizeof(key)) == 0 &&
                     keyLength == sizeof(key);
            }
            ok = ok && mbedtls_gcm_setkey(gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
                 mbedtls_gcm_auth_decrypt(gcm, PLAINTEXT_BYTES, envelope.iv, ENVELOPE_IV_BYTES, (const unsigned char*)aad, strlen(aad), envelope.tag, ENVELOPE_TAG_BYTES,
                                          envelope.ct, plaintext) == 0;
            mbedtls_platform_zeroize(key, sizeof(key));
            mbedtls_gcm_free(gcm);
            mbedtls_pk_free(pk);
            mbedtls_ctr_drbg_free(drbg);
            mbedtls_entropy_free(entropy);
        }
        if (pem != nullptr) mbedtls_platform_zeroize(pem, PEM_MAX);
        free(pem);
        free(pk);
        free(entropy);
        free(drbg);
        free(gcm);
        if (!ok) mbedtls_platform_zeroize(plaintext, PLAINTEXT_BYTES);
        if (ok) return Decrypt::OK;
        return keyReady ? Decrypt::FAILED : Decrypt::UNAVAILABLE;
    }

    static bool _issuedAt(JsonVariantConst value, uint64_t* out) {
        if (value.is<uint64_t>()) {
            *out = value.as<uint64_t>();
            return *out > 0;
        }
        return value.is<const char*>() && parseIssuedAt(value.as<const char*>(), out);
    }

    // Replay, kid and free-slot checks under the lock (spec order). APPLY means
    // decrypt and then _commitSet; anything else has filled `outcome`.
    static SetVerdict _decideSetLocked(Preferences& preferences, const SlotMeta& meta, uint64_t issuedAt, const char* kid, Outcome& outcome,
                                       int* slot) {
        if (!_applyCeiling(preferences)) {
            _fail(outcome, "FAILED", "NVS_WRITE_FAILED", "Could not persist the replay high-water mark");
            return SetVerdict::STALE;
        }
        int index = _find(meta.name);
        bool sameSlot = index >= 0 && _slots[index].setAt == issuedAt && memcmp(_slots[index].ip, meta.ip, 4) == 0 && _slots[index].port == meta.port &&
                        _slots[index].hasMac == meta.hasMac && (!meta.hasMac || memcmp(_slots[index].mac, meta.mac, 6) == 0);
        SetVerdict verdict = decideSet(_clockSynced(), _now(), _hwm, issuedAt, sameSlot);
        switch (verdict) {
            case SetVerdict::CLOCK_NOT_SYNCED: _fail(outcome, "REJECTED", "CLOCK_NOT_SYNCED", "The device clock is not synchronised"); break;
            case SetVerdict::STALE: _fail(outcome, "REJECTED", "SECRET_STALE", "hwm=%llu", (unsigned long long)_hwm); break;
            case SetVerdict::CLOCK_SKEW: _fail(outcome, "REJECTED", "SECRET_CLOCK_SKEW", "hwm=%llu", (unsigned long long)_hwm); break;
            case SetVerdict::ALREADY_APPLIED:
                outcome.setAt = issuedAt;
                outcome.applied = false;
                break;
            case SetVerdict::APPLY:
                if (_kid[0] == '\0' || strcmp(kid, _kid) != 0) {
                    _fail(outcome, "REJECTED", "SECRET_KEY_MISMATCH", "kid does not match this device certificate");
                    return SetVerdict::STALE;
                }
                if (index < 0) index = _free();
                if (index < 0) {
                    _fail(outcome, "REJECTED", "SECRET_STORE_FULL", "%s", STORE_FULL_DETAIL);
                    return SetVerdict::STALE;
                }
                *slot = index;
                break;
        }
        return verdict;
    }

    static SetVerdict _decideSet(const SlotMeta& meta, uint64_t issuedAt, const char* kid, Outcome& outcome) {
        if (!_lockLoaded(outcome)) return SetVerdict::STALE;
        Preferences preferences;
        SetVerdict verdict = SetVerdict::STALE;
        int slot = -1;
        if (!preferences.begin(PREFERENCES_NAMESPACE_SECRETS, false)) {
            _fail(outcome, "FAILED", "NVS_WRITE_FAILED", "Could not open the secret store");
        } else {
            verdict = _decideSetLocked(preferences, meta, issuedAt, kid, outcome, &slot);
            preferences.end();
        }
        _unlock();
        return verdict;
    }

    static void _commitSet(const SlotMeta& meta, const uint8_t* value, size_t length, uint64_t issuedAt, const char* kid, Outcome& outcome) {
        if (!_lockLoaded(outcome)) return;
        Preferences preferences;
        int slot = -1;
        if (!preferences.begin(PREFERENCES_NAMESPACE_SECRETS, false)) {
            _fail(outcome, "FAILED", "NVS_WRITE_FAILED", "Could not open the secret store");
        } else {
            if (_decideSetLocked(preferences, meta, issuedAt, kid, outcome, &slot) == SetVerdict::APPLY) {
                if (!_write(preferences, slot, meta, value, length)) {
                    _fail(outcome, "FAILED", "NVS_WRITE_FAILED", "Could not write the secret slot");
                } else {
                    outcome.setAt = issuedAt;
                    outcome.applied = true;
                }
            }
            preferences.end();
        }
        _unlock();
    }

    void setFromEnvelope(JsonObjectConst args, Outcome& outcome) {
        JsonObjectConst env = args["env"].as<JsonObjectConst>();
        const char* name = args["name"] | (const char*)nullptr;
        const char* ip = args["ip"] | (const char*)nullptr;
        const char* mac = args["mac"] | "";
        uint64_t issuedAt = 0;
        Envelope* envelope = (Envelope*)ps_malloc(sizeof(Envelope));
        uint8_t* plaintext = (uint8_t*)ps_malloc(PLAINTEXT_BYTES);
        if (envelope == nullptr || plaintext == nullptr) {
            free(envelope);
            free(plaintext);
            _fail(outcome, "FAILED", "NO_MEMORY", "Could not allocate the envelope buffers");
            return;
        }

        bool shaped = name != nullptr && ip != nullptr && args["port"].is<int64_t>() && (args["mac"].isNull() || args["mac"].is<const char*>()) &&
                      _issuedAt(args["issued_at"], &issuedAt) && !env.isNull() && env["v"].is<int>() && env["alg"].is<const char*>() &&
                      env["kid"].is<const char*>() && strlen(env["kid"].as<const char*>()) == 64 && _decodeExact(env["ek"] | (const char*)nullptr, envelope->ek, ENVELOPE_EK_BYTES) &&
                      _decodeExact(env["iv"] | (const char*)nullptr, envelope->iv, ENVELOPE_IV_BYTES) &&
                      _decodeExact(env["ct"] | (const char*)nullptr, envelope->ct, PLAINTEXT_BYTES) &&
                      _decodeExact(env["tag"] | (const char*)nullptr, envelope->tag, ENVELOPE_TAG_BYTES);
        SlotMeta meta = {};
        if (!shaped) {
            _fail(outcome, "REJECTED", "BAD_ARGS", "Expected name, ip, port, issued_at, optional mac and env {v, alg, kid, ek, iv, ct, tag}");
        } else if (env["v"].as<int>() != 1 || strcmp(env["alg"].as<const char*>(), ALG) != 0) {
            _fail(outcome, "REJECTED", "UNSUPPORTED_ENVELOPE", "Only v 1 %s is supported", ALG);
        } else if (_validateMeta(name, ip, args["port"].as<int64_t>(), mac, &meta, outcome)) {
            snprintf(outcome.name, sizeof(outcome.name), "%s", name);
            meta.setAt = issuedAt;
            const char* envelopeKid = env["kid"].as<const char*>();
            // The RSA operation runs outside the lock so REST and the shadow report never
            // wait on it; the replay decision is taken again before the write.
            if (_decideSet(meta, issuedAt, envelopeKid, outcome) == SetVerdict::APPLY) {
                char aad[AAD_MAX];
                const uint8_t* value = nullptr;
                size_t length = 0;
                Decrypt decrypted = buildAad(aad, sizeof(aad), DEVICE_ID, name, meta.ip, meta.port, meta.hasMac ? meta.mac : nullptr, issuedAt) == 0
                                        ? Decrypt::FAILED
                                        : _decrypt(*envelope, aad, plaintext);
                if (decrypted == Decrypt::UNAVAILABLE) {
                    _fail(outcome, "FAILED", "NO_MEMORY", "Could not prepare the device key");
                } else if (decrypted == Decrypt::FAILED) {
                    _fail(outcome, "REJECTED", "SECRET_DECRYPT_FAILED", "%s", DECRYPT_FAILED_DETAIL);
                } else if (!unpadPlaintext(plaintext, PLAINTEXT_BYTES, &value, &length) || !isValidValue(value, length)) {
                    _fail(outcome, "REJECTED", "BAD_SECRET_VALUE", "The decrypted value breaks the value rules");
                } else {
                    _commitSet(meta, value, length, issuedAt, envelopeKid, outcome);
                }
            }
        }
        mbedtls_platform_zeroize(plaintext, PLAINTEXT_BYTES);
        mbedtls_platform_zeroize(envelope, sizeof(Envelope));
        free(plaintext);
        free(envelope);
    }

    enum class Erase { DELETED, ABSENT, FAILED };

    // Caller holds the lock and an open namespace. The hwm bump comes first: a delete
    // whose bump is lost would reopen replay of the deleted value inside the window.
    static Erase _erase(Preferences& preferences, const char* name) {
        uint64_t hwm = hwmAfterDelete(_clockSynced(), _now(), _hwm);
        if (hwm != _hwm) {
            if (!_persistHwm(preferences, hwm)) return Erase::FAILED;
            _hwm = hwm;
        }
        int index = _find(name);
        if (index < 0) return Erase::ABSENT;
        char key[4];
        _slotKey((size_t)index, key, sizeof(key));
        if (!preferences.remove(key)) return Erase::FAILED;
        _used[index] = false;
        return Erase::DELETED;
    }

    static void _removeLocked(const char* name, Outcome& outcome) {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_SECRETS, false)) {
            _fail(outcome, "FAILED", "NVS_WRITE_FAILED", "Could not open the secret store");
            return;
        }
        Erase erased = _applyCeiling(preferences) ? _erase(preferences, name) : Erase::FAILED;
        preferences.end();
        if (erased == Erase::FAILED) _fail(outcome, "FAILED", "NVS_WRITE_FAILED", "Could not update the secret store");
        outcome.deleted = erased == Erase::DELETED;
    }

    bool removeLocal(const char* name, Outcome& outcome) {
        if (!isValidName(name)) {
            _fail(outcome, "REJECTED", "BAD_ARGS", "%s", NAME_RULE_DETAIL);
            return false;
        }
        snprintf(outcome.name, sizeof(outcome.name), "%s", name);
        if (!_lockLoaded(outcome)) return false;
        _removeLocked(name, outcome);
        _unlock();
        return strcmp(outcome.status, "SUCCEEDED") == 0;
    }

    void remove(JsonObjectConst args, Outcome& outcome) { removeLocal(args["name"] | (const char*)nullptr, outcome); }

    bool setLocal(const char* name, const char* ip, int64_t port, const char* mac, const uint8_t* value, size_t length, Outcome& outcome) {
        SlotMeta meta = {};
        if (!_validateMeta(name, ip, port, mac, &meta, outcome)) return false;
        if (!isValidValue(value, length)) {
            _fail(outcome, "REJECTED", "BAD_ARGS", "value must be 1-256 bytes of UTF-8 without NUL, CR or LF");
            return false;
        }
        snprintf(outcome.name, sizeof(outcome.name), "%s", name);
        if (!_lockLoaded(outcome)) return false;
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_SECRETS, false)) {
            _fail(outcome, "FAILED", "NVS_WRITE_FAILED", "Could not open the secret store");
        } else {
            bool ceilingOk = _applyCeiling(preferences);
            int index = _find(name);
            if (index < 0) index = _free();
            meta.setAt = localSetAt(_clockSynced(), _now(), _hwm);
            if (!ceilingOk) {
                _fail(outcome, "FAILED", "NVS_WRITE_FAILED", "Could not persist the replay high-water mark");
            } else if (index < 0) {
                _fail(outcome, "REJECTED", "SECRET_STORE_FULL", "%s", STORE_FULL_DETAIL);
            } else if (!_write(preferences, index, meta, value, length)) {
                _fail(outcome, "FAILED", "NVS_WRITE_FAILED", "Could not write the secret slot");
            } else {
                outcome.setAt = meta.setAt;
                outcome.applied = true;
            }
            preferences.end();
        }
        _unlock();
        return strcmp(outcome.status, "SUCCEEDED") == 0;
    }

    // Metadata
    // ========

    static void _ipText(const uint8_t ip[4], char* out, size_t size) { snprintf(out, size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]); }

    // Slot indices of the used slots, sorted by name. Caller holds the lock.
    static size_t _sortedSlots(uint8_t order[SLOT_COUNT]) {
        size_t count = 0;
        for (size_t i = 0; i < SLOT_COUNT; i++) {
            if (!_used[i]) continue;
            size_t at = count++;
            while (at > 0 && strcmp(_slots[order[at - 1]].name, _slots[i].name) > 0) {
                order[at] = order[at - 1];
                at--;
            }
            order[at] = (uint8_t)i;
        }
        return count;
    }

    bool metadataToJson(JsonObject out) {
        if (!_lock()) return false;
        out["kid"] = _kid;
        out["hwm"] = _hwm;
        out["max"] = SLOT_COUNT;
        JsonArray list = out["slots"].to<JsonArray>();
        uint8_t order[SLOT_COUNT];
        size_t count = _sortedSlots(order);
        for (size_t k = 0; k < count; k++) {
            const SlotMeta& slot = _slots[order[k]];
            JsonObject entry = list.add<JsonObject>();
            char text[GatewayLogic::MAC_TEXT_BUFFER] = "";
            entry["name"] = slot.name;
            _ipText(slot.ip, text, sizeof(text));
            entry["ip"] = text;
            entry["port"] = slot.port;
            text[0] = '\0';
            if (slot.hasMac) GatewayLogic::formatMac(slot.mac, text, sizeof(text));
            entry["mac"] = text;
            entry["set_at"] = slot.setAt;
        }
        _unlock();
        return true;
    }

    // Positional slots keep the whole system shadow under 8 KB (design D8).
    bool shadowToJson(JsonObject out) {
        if (!_lock()) return false;
        out["kid"] = _kid;
        out["hwm"] = _hwm;
        JsonArray list = out["slots"].to<JsonArray>();
        uint8_t order[SLOT_COUNT];
        size_t count = _sortedSlots(order);
        for (size_t k = 0; k < count; k++) {
            const SlotMeta& slot = _slots[order[k]];
            JsonArray row = list.add<JsonArray>();
            char target[24];
            snprintf(target, sizeof(target), "%u.%u.%u.%u:%u", slot.ip[0], slot.ip[1], slot.ip[2], slot.ip[3], slot.port);
            row.add(slot.name);
            row.add(target);
            row.add(slot.setAt);
            row.add(slot.hasMac ? 1 : 0);
        }
        _unlock();
        return true;
    }

    // Resolver
    // ========

    void RequestState::clear() {
        for (size_t i = 0; i < count; i++) {
            if (tokens[i].raw != nullptr) mbedtls_platform_zeroize(tokens[i].raw, tokens[i].rawLength);
            if (tokens[i].encoded != nullptr) mbedtls_platform_zeroize(tokens[i].encoded, tokens[i].encodedLength);
            free(tokens[i].raw);
            free(tokens[i].encoded);
            tokens[i] = {};
        }
        count = 0;
        placeholders = 0;
    }

    static GatewayLogic::ResolveStatus _resolve(void* context, const GatewayLogic::ResolveContext& request, const char* name, const char* arg,
                                                GatewayLogic::ResolveOutput* out) {
        if (strcmp(name, "secret") != 0) return GatewayLogic::ResolveStatus::UNKNOWN;
        RequestState* state = (RequestState*)context;
        if (!_lock()) {
            out->reasonCode = "NO_MEMORY";
            snprintf(out->reasonDetail, sizeof(out->reasonDetail), "Secret store busy");
            return GatewayLogic::ResolveStatus::FAILED;
        }
        if (!_loaded) {
            _unlock();
            out->reasonCode = "NO_MEMORY";
            snprintf(out->reasonDetail, sizeof(out->reasonDetail), "Secret store not loaded");
            return GatewayLogic::ResolveStatus::FAILED;
        }
        Decision decision = decide(request, arg, _slots, _used, SLOT_COUNT, state->placeholders);
        bool ok = decision.reasonCode == nullptr;
        if (ok) {
            char key[4];
            _slotKey((size_t)decision.slot, key, sizeof(key));
            uint8_t* blob = (uint8_t*)ps_malloc(SLOT_MAX_BYTES);
            Preferences preferences;
            size_t size = 0;
            if (blob != nullptr && preferences.begin(PREFERENCES_NAMESPACE_SECRETS, true)) {
                size = preferences.getBytes(key, blob, SLOT_MAX_BYTES);
                preferences.end();
            }
            SlotMeta meta;
            const uint8_t* value = nullptr;
            size_t length = 0;
            ok = size > 0 && decodeSlot(blob, size, &meta, &value, &length) && length <= out->cap;
            if (ok) {
                memcpy(out->value, value, length);
                out->length = length;
                out->sensitive = true;
                out->requireMac = decision.requireMac;
                memcpy(out->mac, _slots[decision.slot].mac, 6);
                out->macReasonCode = "SECRET_TARGET_MISMATCH";
                state->placeholders++;
            } else {
                decision.reasonCode = "SECRET_NOT_FOUND";
                decision.reasonDetail = "The secret slot could not be read";
            }
            if (blob != nullptr) mbedtls_platform_zeroize(blob, SLOT_MAX_BYTES);
            free(blob);
        }
        _unlock();
        if (ok) return GatewayLogic::ResolveStatus::OK;
        out->reasonCode = decision.reasonCode;
        // MAC_REQUIRED / TARGET_MAC_CHANGED stay bare code tokens the cloud matches on.
        if (strchr(decision.reasonDetail, ' ') == nullptr) snprintf(out->reasonDetail, sizeof(out->reasonDetail), "%s", decision.reasonDetail);
        else snprintf(out->reasonDetail, sizeof(out->reasonDetail), "%s (%s)", decision.reasonDetail, arg);
        return GatewayLogic::ResolveStatus::FAILED;
    }

    // Records the raw value, then the form actually sent, of each expanded token for the scrub.
    // False makes the expansion fail before any send: an unrecorded token could not be scrubbed.
    static bool _expanded(void* context, const char* name, const char* arg, const uint8_t* bytes, size_t length, bool final) {
        RequestState* state = (RequestState*)context;
        if (strcmp(name, "secret") != 0) return true;
        if (!final) {
            if (state->count >= RequestState::TOKENS_MAX) return false;
            RequestState::Token& token = state->tokens[state->count];
            token.raw = (uint8_t*)ps_malloc(length);
            if (token.raw == nullptr) return false;
            memcpy(token.raw, bytes, length);
            token.rawLength = length;
            snprintf(token.name, sizeof(token.name), "%s", arg);
            state->count++;
            return true;
        }
        if (state->count == 0) return false;
        RequestState::Token& token = state->tokens[state->count - 1];
        if (token.encoded != nullptr || strcmp(token.name, arg) != 0) return false;
        token.encoded = (uint8_t*)ps_malloc(length);
        if (token.encoded == nullptr) return false;
        memcpy(token.encoded, bytes, length);
        token.encodedLength = length;
        return true;
    }

    GatewayLogic::Resolver resolver(RequestState* state) { return {_resolve, state, _expanded}; }

    size_t scrub(const RequestState& state, uint8_t* buffer, size_t length, size_t cap, bool* truncated, bool* redacted) {
        if (state.count == 0 || buffer == nullptr || cap == 0) return length;
        ScrubForm forms[RequestState::TOKENS_MAX * 2];
        size_t formCount = 0;
        for (size_t i = 0; i < state.count; i++) {
            forms[formCount++] = {state.tokens[i].raw, state.tokens[i].rawLength, state.tokens[i].name};
            if (state.tokens[i].encoded != nullptr) forms[formCount++] = {state.tokens[i].encoded, state.tokens[i].encodedLength, state.tokens[i].name};
        }
        uint8_t* scratch = (uint8_t*)ps_malloc(cap);
        if (scratch == nullptr) {
            // Fail closed: publish nothing rather than a possible echo.
            *truncated = true;
            *redacted = true;
            return 0;
        }
        size_t out = SecretsLogic::scrub(buffer, length, cap, scratch, forms, formCount, truncated, redacted);
        mbedtls_platform_zeroize(scratch, cap);
        free(scratch);
        return out;
    }

    // Runs in the executor for every caller: finds this request's secret state in its chain.
    static void _scrubResult(const GatewayExec::Request& request, GatewayExec::Result& result) {
        const RequestState* state = nullptr;
        for (size_t i = 0; i < request.resolverCount && state == nullptr; i++) {
            if (request.resolvers[i].resolve == _resolve) state = (const RequestState*)request.resolvers[i].context;
        }
        if (state == nullptr || state->count == 0) return;
        bool redacted = false;
        if (result.headers != nullptr && result.headersLength > 0) {
            result.headersLength = scrub(*state, (uint8_t*)result.headers, result.headersLength, GatewayExec::HEADER_BLOCK_MAX, &result.truncated, &redacted);
            result.headers[result.headersLength] = '\0';
        }
        if (result.body != nullptr && result.bodyLength > 0) {
            result.bodyLength = scrub(*state, result.body, result.bodyLength, request.maxBytes, &result.truncated, &redacted);
        }
        result.redacted = redacted;
    }
}
