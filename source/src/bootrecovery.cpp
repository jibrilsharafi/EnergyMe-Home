// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "bootrecovery.h"

#include <Preferences.h>
#include <Update.h>
#include <esp_app_desc.h>
#include <esp_attr.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "app_image_descriptor.h"
#include "utils.h"

using BootRecoveryLogic::Action;
using BootRecoveryLogic::Decision;
using BootRecoveryLogic::ResetKind;
using BootRecoveryLogic::Stage;

namespace BootRecovery
{
    // Set right before a deliberate restart, consumed by the next boot. RTC memory
    // survives every software reset; a power-on leaves garbage, but power-on is
    // classified Neutral and never reads it as a verdict.
    static constexpr uint32_t DELIBERATE_RESTART_MAGIC = 0xB007DE1Bu;
    RTC_NOINIT_ATTR static uint32_t _deliberateRestart;

    // Written once by begin() before any other task exists, then only by
    // requestExit()/markStable(): single-byte values, read without a lock.
    static Stage _stage = Stage::Normal;
    static uint8_t _unstableBoots = 0;

    static ResetKind _classifyReset(esp_reset_reason_t reason) {
        switch (reason) {
            case ESP_RST_POWERON:
            // A sagging supply is not a firmware loop, and a wipe would not fix it:
            // a storm of brownouts must never cost the user their configuration
            case ESP_RST_BROWNOUT:
            case ESP_RST_PWR_GLITCH:
            case ESP_RST_EXT:
            case ESP_RST_USB:
            case ESP_RST_JTAG:
            case ESP_RST_DEEPSLEEP:
                return ResetKind::Neutral;
            case ESP_RST_SW:
                return ResetKind::Software;
            default:
                return ResetKind::Fault; // Panics, watchdogs and anything unknown
        }
    }

    static void _runningFirmwareId(char *buffer, size_t bufferSize) {
        const esp_app_desc_t *desc = esp_app_get_description();
        size_t offset = 0;
        for (size_t i = 0; i < BOOT_RECOVERY_FIRMWARE_ID_BYTES && offset + 2 < bufferSize; i++) {
            offset += snprintf(buffer + offset, bufferSize - offset, "%02x", desc->app_elf_sha256[i]);
        }
    }

    static bool _debugFlag(Preferences &prefs, const char *key) {
#ifdef ENV_DEV
        return prefs.getBool(key, false);
#else
        (void)prefs;
        (void)key;
        return false;
#endif
    }

    static uint8_t _readHistory(Preferences &prefs, BootRecoveryLogic::HistoryEntry *entries) {
        size_t length = prefs.getBytesLength(BOOT_RECOVERY_HISTORY_KEY);
        if (length == 0 || length % sizeof(BootRecoveryLogic::HistoryEntry) != 0 ||
            length > sizeof(BootRecoveryLogic::HistoryEntry) * BootRecoveryLogic::HISTORY_CAPACITY) return 0;
        prefs.getBytes(BOOT_RECOVERY_HISTORY_KEY, entries, length);
        return (uint8_t)(length / sizeof(BootRecoveryLogic::HistoryEntry));
    }

    static void _appendHistory(Preferences &prefs, const BootRecoveryLogic::HistoryEntry &entry) {
        BootRecoveryLogic::HistoryEntry entries[BootRecoveryLogic::HISTORY_CAPACITY] = {};
        uint8_t count = BootRecoveryLogic::pushHistory(entries, _readHistory(prefs, entries), entry);
        prefs.putBytes(BOOT_RECOVERY_HISTORY_KEY, entries, count * sizeof(BootRecoveryLogic::HistoryEntry));
    }

    static bool _rollbackAllowed(bool autoRollbackDone, bool debugNoRollback) {
        if (debugNoRollback) {
            Serial.println("[BootRecovery] Automatic rollback disabled by the debug boot crash");
            return false;
        }
        if (autoRollbackDone || !Update.canRollBack()) return false;
        ImageDescriptor::Verdict verdict =
            AppImageDescriptor::validatePartition(esp_ota_get_next_update_partition(NULL), false);
        // Strictly ACCEPT. A factory device keeps the jig's provisioning firmware in
        // app0, which carries no descriptor and passes on Home as a legacy image: an
        // automatic rollback must never boot a customer device into it. Dev images on
        // a prod device are refused for the same reason (nobody chose them).
        if (verdict != ImageDescriptor::Verdict::ACCEPT) {
            Serial.printf("[BootRecovery] No automatic rollback onto the passive image (%s)\n",
                          ImageDescriptor::verdictToString(verdict));
            return false;
        }
        return true;
    }

    static void _restartDeliberately() {
        markDeliberateRestart();
        Serial.flush();
        ESP.restart();
    }

    void begin() {
        bool deliberate = _deliberateRestart == DELIBERATE_RESTART_MAGIC;
        _deliberateRestart = 0;
        esp_reset_reason_t reason = esp_reset_reason();

        char firmwareId[BOOT_RECOVERY_FIRMWARE_ID_BUFFER_SIZE] = {};
        _runningFirmwareId(firmwareId, sizeof(firmwareId));

        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_RECOVERY, false)) {
            // Without NVS there is nothing to count with; boot normally rather than guess
            Serial.println("[BootRecovery] Cannot open the recovery namespace, ladder disabled this boot");
            return;
        }

        char storedFirmwareId[BOOT_RECOVERY_FIRMWARE_ID_BUFFER_SIZE] = {};
        prefs.getString(BOOT_RECOVERY_FIRMWARE_KEY, storedFirmwareId, sizeof(storedFirmwareId));
        bool firmwareMatches = strcmp(storedFirmwareId, firmwareId) == 0;
        Stage storedStage = BootRecoveryLogic::stageFromByte(prefs.getUChar(BOOT_RECOVERY_STAGE_KEY, 0));
        uint8_t storedUnstable = prefs.getUChar(BOOT_RECOVERY_UNSTABLE_KEY, 0);
        bool autoRollbackDone = prefs.getBool(BOOT_RECOVERY_AUTO_ROLLBACK_KEY, false);
        bool debugNoRollback = firmwareMatches && _debugFlag(prefs, BOOT_RECOVERY_DEBUG_NO_ROLLBACK_KEY);
        bool debugKeepWifi = firmwareMatches && _debugFlag(prefs, BOOT_RECOVERY_DEBUG_KEEP_WIFI_KEY);

        BootRecoveryLogic::Input input = {
            storedStage, storedUnstable, firmwareMatches, _classifyReset(reason), deliberate, false
        };
        Decision decision = BootRecoveryLogic::decide(input);
        if (decision.action == Action::FactoryReset && _rollbackAllowed(autoRollbackDone, debugNoRollback)) {
            input.rollbackAllowed = true;
            decision = BootRecoveryLogic::decide(input);
        }

        Serial.printf("[BootRecovery] Reset reason %d%s | stage %s -> %s | unstable boots %u -> %u%s\n",
                      (int)reason, deliberate ? " (deliberate)" : "",
                      BootRecoveryLogic::stageToString(storedStage), BootRecoveryLogic::stageToString(decision.stage),
                      storedUnstable, decision.unstableBoots, firmwareMatches ? "" : " | new firmware");

        if (!firmwareMatches) {
            prefs.putString(BOOT_RECOVERY_FIRMWARE_KEY, firmwareId);
            // A reflash always gets a bench device back
            prefs.remove(BOOT_RECOVERY_DEBUG_CRASH_KEY);
            prefs.remove(BOOT_RECOVERY_DEBUG_KEEP_WIFI_KEY);
            prefs.remove(BOOT_RECOVERY_DEBUG_NO_ROLLBACK_KEY);
        }
        if (!firmwareMatches || decision.stage != storedStage) prefs.putUChar(BOOT_RECOVERY_STAGE_KEY, (uint8_t)decision.stage);
        if (!firmwareMatches || decision.unstableBoots != storedUnstable) prefs.putUChar(BOOT_RECOVERY_UNSTABLE_KEY, decision.unstableBoots);

        _stage = decision.stage;
        _unstableBoots = decision.unstableBoots;

        BootRecoveryLogic::HistoryEntry entry = {
            (uint8_t)reason, deliberate, !firmwareMatches, (uint8_t)storedStage,
            (uint8_t)decision.stage, decision.unstableBoots, (uint8_t)decision.action
        };
        // A rollback that fails falls back to a wipe below and records that instead
        if (decision.action != Action::RollBack) _appendHistory(prefs, entry);

        if (decision.action == Action::RollBack) {
            prefs.putBool(BOOT_RECOVERY_AUTO_ROLLBACK_KEY, true);
            _appendHistory(prefs, entry);
            prefs.end();
            Serial.println("[BootRecovery] Boot loop: rolling back to the previous firmware");
            if (Update.rollBack()) _restartDeliberately();
            Serial.printf("[BootRecovery] Rollback failed (%s), factory resetting instead\n", Update.errorString());
            if (!prefs.begin(PREFERENCES_NAMESPACE_RECOVERY, false)) return;
            prefs.putUChar(BOOT_RECOVERY_STAGE_KEY, (uint8_t)Stage::Wiped);
            _stage = Stage::Wiped;
            decision.action = Action::FactoryReset;
            entry.toStage = (uint8_t)Stage::Wiped;
            entry.action = (uint8_t)Action::FactoryReset;
            _appendHistory(prefs, entry);
        }

        prefs.end();

        if (decision.action == Action::FactoryReset) {
            Serial.println("[BootRecovery] Boot loop: factory reset");
            if (debugKeepWifi) Serial.println("[BootRecovery] Debug boot crash: the wipe keeps WiFi");
            factoryResetAtBoot(debugKeepWifi);
            _restartDeliberately();
        }

        if (isDegraded()) {
            Serial.printf("[BootRecovery] Degraded boot (%s): meter passive%s\n",
                          BootRecoveryLogic::stageToString(_stage), cloudDisabled() ? ", cloud disabled" : "");
        }
    }

    Stage stage() { return _stage; }
    bool meterPassive() { return BootRecoveryLogic::meterPassive(_stage); }
    bool cloudDisabled() { return BootRecoveryLogic::cloudDisabled(_stage); }
    bool isDegraded() { return BootRecoveryLogic::isDegraded(_stage); }

    void markDeliberateRestart() {
        _deliberateRestart = DELIBERATE_RESTART_MAGIC;
    }

    void markStable() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_RECOVERY, false)) {
            LOG_WARNING("Cannot open the recovery namespace to mark the boot stable");
            return;
        }
        if (prefs.getUChar(BOOT_RECOVERY_UNSTABLE_KEY, 0) != 0) prefs.putUChar(BOOT_RECOVERY_UNSTABLE_KEY, 0);
        if (prefs.getBool(BOOT_RECOVERY_AUTO_ROLLBACK_KEY, false)) prefs.putBool(BOOT_RECOVERY_AUTO_ROLLBACK_KEY, false);
        // A full boot that holds proves the wipe (or the recovery exit) worked: the
        // next loop on this firmware starts the ladder from the top again
        if (_stage == Stage::Wiped) {
            prefs.putUChar(BOOT_RECOVERY_STAGE_KEY, (uint8_t)Stage::Normal);
            _stage = Stage::Normal;
        }
        prefs.end();
        _unstableBoots = 0;
        LOG_DEBUG("Boot marked stable for the boot-loop ladder");
    }

    void setAutoRollbackBlocked(bool blocked) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_RECOVERY, false)) {
            LOG_WARNING("Cannot open the recovery namespace to %s automatic rollback", blocked ? "block" : "allow");
            return;
        }
        prefs.putBool(BOOT_RECOVERY_AUTO_ROLLBACK_KEY, blocked);
        prefs.end();
    }

    bool requestExit() {
        if (!isDegraded()) return false;
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_RECOVERY, false)) {
            LOG_ERROR("Cannot open the recovery namespace to leave recovery mode");
            return false;
        }
        bool written = prefs.putUChar(BOOT_RECOVERY_STAGE_KEY, (uint8_t)Stage::Wiped) > 0;
        prefs.putUChar(BOOT_RECOVERY_UNSTABLE_KEY, 0);
        prefs.end();
        if (!written) {
            LOG_ERROR("Failed to persist the recovery exit");
            return false;
        }
        LOG_WARNING("Leaving recovery mode (%s): the next boot is a full one", BootRecoveryLogic::stageToString(_stage));
        return true;
    }

    void toJson(JsonDocument &doc) {
        doc["stage"] = BootRecoveryLogic::stageToString(_stage);
        doc["unstableBoots"] = _unstableBoots;
        doc["unstableBootsToEscalate"] = BootRecoveryLogic::UNSTABLE_BOOTS_TO_ESCALATE;
        doc["degraded"] = isDegraded();
        doc["meterPassive"] = meterPassive();
        doc["cloudDisabled"] = cloudDisabled();

        // Oldest first: the boots that led here, readable after the fact without a serial console
        JsonArray history = doc["history"].to<JsonArray>();
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_RECOVERY, true)) return;
        BootRecoveryLogic::HistoryEntry entries[BootRecoveryLogic::HISTORY_CAPACITY] = {};
        uint8_t count = _readHistory(prefs, entries);
        prefs.end();
        for (uint8_t i = 0; i < count; i++) {
            JsonObject item = history.add<JsonObject>();
            item["resetReason"] = entries[i].resetReason;
            item["deliberate"] = entries[i].deliberate != 0;
            item["newFirmware"] = entries[i].newFirmware != 0;
            item["from"] = BootRecoveryLogic::stageToString(BootRecoveryLogic::stageFromByte(entries[i].fromStage));
            item["to"] = BootRecoveryLogic::stageToString(BootRecoveryLogic::stageFromByte(entries[i].toStage));
            item["unstableBoots"] = entries[i].unstableBoots;
            item["action"] = BootRecoveryLogic::actionToString((Action)entries[i].action);
        }
    }

#ifdef ENV_DEV
    void injectBootCrashIfArmed() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_RECOVERY, true)) return;
        uint8_t untilStage = prefs.getUChar(BOOT_RECOVERY_DEBUG_CRASH_KEY, 0);
        prefs.end();
        if ((uint8_t)_stage >= untilStage) return;
        Serial.printf("[BootRecovery] Debug boot crash armed until stage %u, aborting at stage %s\n",
                      untilStage, BootRecoveryLogic::stageToString(_stage));
        Serial.flush();
        abort();
    }

    bool setBootCrashThreshold(uint8_t untilStage, bool keepWifi, bool noRollback) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_RECOVERY, false)) return false;
        bool ok;
        if (untilStage == 0) {
            prefs.remove(BOOT_RECOVERY_DEBUG_CRASH_KEY);
            prefs.remove(BOOT_RECOVERY_DEBUG_KEEP_WIFI_KEY);
            prefs.remove(BOOT_RECOVERY_DEBUG_NO_ROLLBACK_KEY);
            ok = !prefs.isKey(BOOT_RECOVERY_DEBUG_CRASH_KEY);
        } else {
            ok = prefs.putUChar(BOOT_RECOVERY_DEBUG_CRASH_KEY, untilStage) > 0 &&
                 prefs.putBool(BOOT_RECOVERY_DEBUG_KEEP_WIFI_KEY, keepWifi) > 0 &&
                 prefs.putBool(BOOT_RECOVERY_DEBUG_NO_ROLLBACK_KEY, noRollback) > 0;
        }
        prefs.end();
        return ok;
    }
#endif
}
