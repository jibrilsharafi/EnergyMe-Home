// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "issueregistry.h"

#include <LittleFS.h>

#include "ade7953.h"
#include "crashmonitor.h"
#include "custommqtt.h"
#include "customtime.h"
#include "customwifi.h"
#include "globals.h"
#include "influxdbclient.h"
#include "mqtt.h"
#include "utils.h"

namespace IssueRegistry
{
    static TaskHeartbeat _heartbeat;

    // Instance table
    // =========================================================
    // A slot with state Inactive is free. Identity is (code, channel);
    // globally-scoped codes use channel = ISSUE_GLOBAL_SCOPE.
    struct IssueInstance {
        IssueLogic::Code code;
        uint8_t channel;
        IssueLogic::State state;
        uint32_t occurrences;     // raise edges seen by this instance
        uint32_t firstSeenUnix;
        uint32_t lastChangeUnix;
        char message[ISSUE_MESSAGE_BUFFER_SIZE];
    };

    static IssueInstance _instances[ISSUE_MAX_INSTANCES] = {};
    static SemaphoreHandle_t _registryMutex = NULL;

    // Per-code evaluator state (tick task only - no mutex needed)
    // =========================================================
    static uint32_t _prevFlipCount[MAX_CHANNEL_COUNT] = {};
    static uint32_t _prevEvidenceReads[MAX_CHANNEL_COUNT] = {};
    static uint32_t _prevClampedReads[MAX_CHANNEL_COUNT] = {};
    static uint16_t _mismatchStreak[MAX_CHANNEL_COUNT] = {};

    static uint16_t _customMqttStreak = 0;
    static uint16_t _cloudMqttStreak = 0;
    static uint16_t _influxStreak = 0;
    static uint64_t _prevInfluxOk = 0;
    static uint64_t _prevInfluxError = 0;
    static uint16_t _heapStreak = 0;
    static uint16_t _voltageStreak = 0;
    static uint16_t _frequencyStreak = 0;
    static uint16_t _readFailureStreak = 0;
    static uint64_t _prevAde7953Failures = 0;
    static bool _littleFsCondition = false;
    static bool _firstTick = true;

    // Task
    static TaskHandle_t _taskHandle = NULL;
    static bool _taskShouldRun = false;

    // Private declarations
    // =========================================================
    static void _registryTask(void *parameter);
    static void _evaluateChannelCodes();
    static void _evaluateGlobalCodes();
    static void _updateInstance(IssueLogic::Code code, uint8_t channel, bool conditionTrue, const char *message);
    static IssueInstance* _findInstance(IssueLogic::Code code, uint8_t channel);
    static IssueInstance* _allocateInstance();
    static void _ackInstanceLocked(IssueInstance &inst);
    static const char* _stateToString(IssueLogic::State state);
    static bool _codeFromString(const char *codeStr, IssueLogic::Code &code);
    static void _logTransition(const char *verb, bool raise, IssueLogic::Code code, uint8_t channel, const char *message);

    // Public API
    // =========================================================

    void begin()
    {
        LOG_DEBUG("Setting up issue registry...");

        if (!createMutexIfNeeded(&_registryMutex)) return;

        if (_taskHandle != NULL) {
            LOG_DEBUG("Issue registry task is already running");
            return;
        }

        LOG_DEBUG("Starting issue registry task with %d bytes stack in internal RAM (reads LittleFS usage)", ISSUE_REGISTRY_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _registryTask,
            ISSUE_REGISTRY_TASK_NAME,
            ISSUE_REGISTRY_TASK_STACK_SIZE,
            NULL,
            ISSUE_REGISTRY_TASK_PRIORITY,
            &_taskHandle);

        if (result != pdPASS) {
            LOG_ERROR("Failed to create issue registry task");
            _taskHandle = NULL;
        }
    }

    void stop()
    {
        stopTaskGracefully(&_taskHandle, "Issue registry task");
    }

    bool issuesToJson(JsonDocument &doc)
    {
        if (!acquireMutex(&_registryMutex)) {
            LOG_ERROR("Failed to acquire registry mutex for issuesToJson");
            return false;
        }

        // The table is tiny (a few KB): building the document under the mutex is
        // memory-only work; network serialization happens outside, on the copy.
        JsonArray issues = doc["issues"].to<JsonArray>();
        uint32_t total = 0, unacked = 0, active = 0;
        IssueLogic::Severity maxUnackedSeverity = IssueLogic::Severity::Info;
        bool hasUnacked = false;

        for (uint8_t i = 0; i < ISSUE_MAX_INSTANCES; i++) {
            const IssueInstance &inst = _instances[i];
            if (!IssueLogic::isVisible(inst.state)) continue;

            JsonObject obj = issues.add<JsonObject>();
            obj["code"] = IssueLogic::codeToString(inst.code);
            if (inst.channel != ISSUE_GLOBAL_SCOPE) obj["channel"] = inst.channel;
            obj["state"] = _stateToString(inst.state);
            obj["severity"] = IssueLogic::severityToString(IssueLogic::codeSeverity(inst.code));
            obj["firstSeenUnix"] = inst.firstSeenUnix;
            obj["lastChangeUnix"] = inst.lastChangeUnix;
            obj["occurrences"] = inst.occurrences;
            obj["message"] = inst.message;

            total++;
            if (IssueLogic::isActive(inst.state)) active++;
            if (IssueLogic::isUnacked(inst.state)) {
                unacked++;
                IssueLogic::Severity sev = IssueLogic::codeSeverity(inst.code);
                if (!hasUnacked || (uint8_t)sev > (uint8_t)maxUnackedSeverity) maxUnackedSeverity = sev;
                hasUnacked = true;
            }
        }

        JsonObject summary = doc["summary"].to<JsonObject>();
        summary["total"] = total;
        summary["unacked"] = unacked;
        summary["active"] = active;
        if (hasUnacked) summary["maxUnackedSeverity"] = IssueLogic::severityToString(maxUnackedSeverity);

        releaseMutex(&_registryMutex);
        return true;
    }

    bool ack(const char *codeStr, uint8_t channel)
    {
        IssueLogic::Code code;
        if (!_codeFromString(codeStr, code)) return false;

        bool acked = false;

        if (!acquireMutex(&_registryMutex)) {
            LOG_ERROR("Failed to acquire registry mutex for ack");
            return false;
        }
        IssueInstance *inst = _findInstance(code, channel);
        if (inst != nullptr && IssueLogic::isUnacked(inst->state)) {
            _ackInstanceLocked(*inst);
            acked = true;
        }
        releaseMutex(&_registryMutex);

        if (acked) _logTransition("acked", false, code, channel, "");
        return acked;
    }

    uint32_t ackAll()
    {
        uint32_t ackedCount = 0;
        // Collect what was acked for logging outside the mutex
        IssueLogic::Code ackedCodes[ISSUE_MAX_INSTANCES];
        uint8_t ackedChannels[ISSUE_MAX_INSTANCES];

        if (!acquireMutex(&_registryMutex)) {
            LOG_ERROR("Failed to acquire registry mutex for ackAll");
            return 0;
        }
        for (uint8_t i = 0; i < ISSUE_MAX_INSTANCES; i++) {
            IssueInstance &inst = _instances[i];
            if (!IssueLogic::isUnacked(inst.state)) continue;
            ackedCodes[ackedCount] = inst.code;
            ackedChannels[ackedCount] = inst.channel;
            ackedCount++;
            _ackInstanceLocked(inst);
        }
        releaseMutex(&_registryMutex);

        for (uint32_t i = 0; i < ackedCount; i++) {
            _logTransition("acked", false, ackedCodes[i], ackedChannels[i], "");
        }
        return ackedCount;
    }

    TaskInfo getTaskInfo()
    {
        return getTaskInfoSafely(_taskHandle, ISSUE_REGISTRY_TASK_STACK_SIZE, &_heartbeat);
    }

    // Task and evaluators
    // =========================================================

    static void _registryTask(void *parameter)
    {
        LOG_DEBUG("Issue registry task started");
        _taskShouldRun = true;

        while (_taskShouldRun) {
            TASK_HEARTBEAT(_heartbeat);

            _evaluateChannelCodes();
            _evaluateGlobalCodes();
            _firstTick = false;

            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ISSUE_REGISTRY_TICK_INTERVAL)) > 0) {
                _taskShouldRun = false;
                break;
            }
        }

        LOG_DEBUG("Issue registry task stopping");
        _taskHandle = NULL;
        vTaskDelete(NULL);
    }

    static void _evaluateChannelCodes()
    {
        char message[ISSUE_MESSAGE_BUFFER_SIZE];
        char label[NAME_BUFFER_SIZE];

        for (uint8_t ch = 0; ch < globalHwProfile->totalChannelCount; ch++) {
            ChannelIssueFacts facts;
            if (!Ade7953::getChannelIssueFacts(ch, facts)) continue;

            if (!Ade7953::isChannelActive(ch)) {
                // Channel disabled: clear streak and force condition false so any
                // existing mismatch instance resolves on the next tick.
                _mismatchStreak[ch] = 0;
                _prevEvidenceReads[ch] = facts.evidenceReads;
                _prevClampedReads[ch] = facts.clampedEvidenceReads;
                _prevFlipCount[ch] = facts.polarityFlipCount;
                _updateInstance(IssueLogic::Code::ChannelPolarityMismatch, ch, false, "");
                _updateInstance(IssueLogic::Code::CtPolarityFlipped, ch, false, "");
                continue;
            }

            // --- ct_polarity_flipped (event): pulse on a new flip, then the
            // instance lands in cleared-unacked and lingers until acked. Flips
            // that happened before the first tick (boot window) still pulse,
            // since _prevFlipCount starts at 0.
            bool flipPulse = (facts.polarityFlipCount != _prevFlipCount[ch]);
            _prevFlipCount[ch] = facts.polarityFlipCount;
            if (flipPulse) {
                Ade7953::getChannelLabel(ch, label, sizeof(label));
                snprintf(message, sizeof(message),
                         "%s: CT orientation auto-corrected (%lu flip%s this boot)",
                         label,
                         (unsigned long)facts.polarityFlipCount,
                         facts.polarityFlipCount == 1 ? "" : "s");
            } else {
                message[0] = '\0';
            }
            _updateInstance(IssueLogic::Code::CtPolarityFlipped, ch, flipPulse, message);

            // --- channel_polarity_mismatch (condition): real readings persistently clamped
            uint32_t evidenceDelta = (uint32_t)IssueLogic::counterDelta(_prevEvidenceReads[ch], facts.evidenceReads);
            uint32_t clampedDelta = (uint32_t)IssueLogic::counterDelta(_prevClampedReads[ch], facts.clampedEvidenceReads);
            _prevEvidenceReads[ch] = facts.evidenceReads;
            _prevClampedReads[ch] = facts.clampedEvidenceReads;

            IssueLogic::Evidence evidence = IssueLogic::channelMismatchEvidence(
                evidenceDelta, clampedDelta,
                ISSUE_MISMATCH_MIN_EVIDENCE_READS, ISSUE_MISMATCH_MIN_CLAMPED_FRACTION);
            _mismatchStreak[ch] = IssueLogic::updateStreak(_mismatchStreak[ch], evidence);
            bool mismatch = (_mismatchStreak[ch] >= ISSUE_STREAK_TO_RAISE);
            if (mismatch) {
                Ade7953::getChannelLabel(ch, label, sizeof(label));
                snprintf(message, sizeof(message),
                         "%s: readings persistently negative and clamped to 0 W. "
                         "The reverse flag and the physical CT orientation disagree - fix either.",
                         label);
            } else {
                message[0] = '\0';
            }
            _updateInstance(IssueLogic::Code::ChannelPolarityMismatch, ch, mismatch, message);
        }
    }

    static void _evaluateGlobalCodes()
    {
        // Messages are only consumed on a raise edge, so each block formats its
        // message only when its condition is true (same idiom as the channel codes).
        char message[ISSUE_MESSAGE_BUFFER_SIZE];

        // --- custom_mqtt_connect_failed: same connection fact + streak hysteresis
        // as cloud MQTT, so both connectivity issues share one mechanism.
        bool customMqttEnabled = CustomMqtt::isEnabled();
        IssueLogic::Evidence customMqttEvidence = (!customMqttEnabled || CustomMqtt::isConnected())
                                                      ? IssueLogic::Evidence::Good
                                                      : IssueLogic::Evidence::Bad;
        _customMqttStreak = IssueLogic::updateStreak(_customMqttStreak, customMqttEvidence);
        bool customMqttDown = (_customMqttStreak >= ISSUE_CONNECTIVITY_STREAK_TO_RAISE);
        message[0] = '\0';
        if (customMqttDown) {
            snprintf(message, sizeof(message), "Custom MQTT enabled but it has been disconnected for over %u s",
                     (unsigned)(ISSUE_CONNECTIVITY_STREAK_TO_RAISE * ISSUE_REGISTRY_TICK_INTERVAL / 1000));
        }
        _updateInstance(IssueLogic::Code::CustomMqttConnectFailed, ISSUE_GLOBAL_SCOPE, customMqttDown, message);

        // --- cloud_mqtt_disconnected
        bool cloudEnabled = Mqtt::isCloudServicesEnabled();
        IssueLogic::Evidence cloudEvidence = (!cloudEnabled || Mqtt::isConnected())
                                                 ? IssueLogic::Evidence::Good
                                                 : IssueLogic::Evidence::Bad;
        _cloudMqttStreak = IssueLogic::updateStreak(_cloudMqttStreak, cloudEvidence);
        bool cloudDown = (_cloudMqttStreak >= ISSUE_CONNECTIVITY_STREAK_TO_RAISE);
        message[0] = '\0';
        if (cloudDown) {
            snprintf(message, sizeof(message), "Cloud services enabled but AWS MQTT has been disconnected for over %u s",
                     (unsigned)(ISSUE_CONNECTIVITY_STREAK_TO_RAISE * ISSUE_REGISTRY_TICK_INTERVAL / 1000));
        }
        _updateInstance(IssueLogic::Code::CloudMqttDisconnected, ISSUE_GLOBAL_SCOPE, cloudDown, message);

        // --- influxdb_upload_failing: windowed deltas of the global statistics counters
        bool influxEnabled = InfluxDbClient::isEnabled();
        uint64_t influxOkDelta = IssueLogic::counterDelta(_prevInfluxOk, statistics.influxdbUploadCount);
        uint64_t influxErrorDelta = IssueLogic::counterDelta(_prevInfluxError, statistics.influxdbUploadCountError);
        _prevInfluxOk = statistics.influxdbUploadCount;
        _prevInfluxError = statistics.influxdbUploadCountError;
        IssueLogic::Evidence influxEvidence = influxEnabled
                                                  ? IssueLogic::rateEvidence(influxOkDelta, influxErrorDelta)
                                                  : IssueLogic::Evidence::Good;
        _influxStreak = IssueLogic::updateStreak(_influxStreak, influxEvidence);
        bool influxFailing = (_influxStreak >= ISSUE_STREAK_TO_RAISE);
        message[0] = '\0';
        if (influxFailing) {
            snprintf(message, sizeof(message), "InfluxDB enabled but uploads keep failing (%u consecutive bad windows)",
                     (unsigned)_influxStreak);
        }
        _updateInstance(IssueLogic::Code::InfluxDbUploadFailing, ISSUE_GLOBAL_SCOPE, influxFailing, message);

        // --- ntp_not_synced (boot grace so a normal cold boot never alarms)
        bool ntpNotSynced = (millis64() > ISSUE_NTP_BOOT_GRACE_MS) && !CustomTime::isTimeSynched();
        _updateInstance(IssueLogic::Code::NtpNotSynced, ISSUE_GLOBAL_SCOPE, ntpNotSynced,
                        "Time never synced via NTP - timestamps are unreliable");

        // --- panic_reboot (event): pulse once on the first tick after a crash reboot
        bool panicPulse = _firstTick && (CrashMonitor::isLastResetDueToCrash() || CrashMonitor::getConsecutiveCrashCount() > 0);
        if (panicPulse) {
            snprintf(message, sizeof(message),
                     "Device rebooted after a crash (%s, %lu crash%s total, %lu consecutive)",
                     getResetReasonString(esp_reset_reason()),
                     (unsigned long)CrashMonitor::getCrashCount(),
                     CrashMonitor::getCrashCount() == 1 ? "" : "es",
                     (unsigned long)CrashMonitor::getConsecutiveCrashCount());
        } else {
            message[0] = '\0';
        }
        _updateInstance(IssueLogic::Code::PanicReboot, ISSUE_GLOBAL_SCOPE, panicPulse, message);

        // --- safe_mode_active
        bool safeMode = CrashMonitor::isInSafeMode();
        _updateInstance(IssueLogic::Code::SafeModeActive, ISSUE_GLOBAL_SCOPE, safeMode,
                        "Safe mode active - restart protection engaged after rapid restarts");

        // --- heap_low
        uint32_t freeHeap = ESP.getFreeHeap();
        IssueLogic::Evidence heapEvidence = (freeHeap < ISSUE_HEAP_LOW_THRESHOLD_BYTES)
                                                ? IssueLogic::Evidence::Bad
                                                : IssueLogic::Evidence::Good;
        _heapStreak = IssueLogic::updateStreak(_heapStreak, heapEvidence);
        bool heapLow = (_heapStreak >= ISSUE_STREAK_TO_RAISE);
        message[0] = '\0';
        if (heapLow) {
            snprintf(message, sizeof(message), "Free internal heap low (%lu bytes, threshold %u)",
                     (unsigned long)freeHeap, (unsigned)ISSUE_HEAP_LOW_THRESHOLD_BYTES);
        }
        _updateInstance(IssueLogic::Code::HeapLow, ISSUE_GLOBAL_SCOPE, heapLow, message);

        // --- littlefs_near_full (raise/clear thresholds differ: simple hysteresis)
        size_t fsTotal = LittleFS.totalBytes();
        size_t fsUsed = LittleFS.usedBytes();
        float fsFraction = (fsTotal > 0) ? ((float)fsUsed / (float)fsTotal) : 0.0f;
        if (fsFraction > ISSUE_LITTLEFS_USED_FRACTION_RAISE) _littleFsCondition = true;
        else if (fsFraction < ISSUE_LITTLEFS_USED_FRACTION_CLEAR) _littleFsCondition = false;
        message[0] = '\0';
        if (_littleFsCondition) {
            snprintf(message, sizeof(message), "LittleFS %.0f%% full (%u of %u KB)",
                     fsFraction * 100.0f, (unsigned)(fsUsed / 1024), (unsigned)(fsTotal / 1024));
        }
        _updateInstance(IssueLogic::Code::LittleFsNearFull, ISSUE_GLOBAL_SCOPE, _littleFsCondition, message);

        // --- voltage_out_of_range (channel 0 carries the only voltage measurement)
        MeterValues meterValues;
        float voltage = 0.0f;
        if (Ade7953::getMeterValues(meterValues, 0)) voltage = meterValues.voltage;
        IssueLogic::Evidence voltageEv = IssueLogic::voltageEvidence(voltage, ISSUE_VOLTAGE_TOLERANCE_FRACTION);
        _voltageStreak = IssueLogic::updateStreak(_voltageStreak, voltageEv);
        bool voltageBad = (_voltageStreak >= ISSUE_STREAK_TO_RAISE);
        message[0] = '\0';
        if (voltageBad) {
            snprintf(message, sizeof(message), "Mains voltage %.1f V is over %.0f%% away from nominal %.0f V",
                     voltage, ISSUE_VOLTAGE_TOLERANCE_FRACTION * 100.0f, IssueLogic::nearestNominalVoltage(voltage));
        }
        _updateInstance(IssueLogic::Code::VoltageOutOfRange, ISSUE_GLOBAL_SCOPE, voltageBad, message);

        // --- grid_frequency_out_of_range
        float gridFrequency = Ade7953::getGridFrequency();
        IssueLogic::Evidence frequencyEv = IssueLogic::frequencyEvidence(gridFrequency, ISSUE_FREQUENCY_TOLERANCE_HZ);
        _frequencyStreak = IssueLogic::updateStreak(_frequencyStreak, frequencyEv);
        bool frequencyBad = (_frequencyStreak >= ISSUE_STREAK_TO_RAISE);
        message[0] = '\0';
        if (frequencyBad) {
            snprintf(message, sizeof(message), "Grid frequency %.2f Hz is over %.1f Hz away from nominal %.0f Hz",
                     gridFrequency, ISSUE_FREQUENCY_TOLERANCE_HZ, IssueLogic::nearestNominalFrequency(gridFrequency));
        }
        _updateInstance(IssueLogic::Code::GridFrequencyOutOfRange, ISSUE_GLOBAL_SCOPE, frequencyBad, message);

        // --- ade7953_read_failures
        uint64_t failureDelta = IssueLogic::counterDelta(_prevAde7953Failures, statistics.ade7953ReadingCountFailure);
        _prevAde7953Failures = statistics.ade7953ReadingCountFailure;
        IssueLogic::Evidence failureEvidence = IssueLogic::readFailureEvidence(failureDelta, ISSUE_ADE7953_FAILURES_PER_WINDOW);
        _readFailureStreak = IssueLogic::updateStreak(_readFailureStreak, failureEvidence);
        bool readsFailing = (_readFailureStreak >= ISSUE_STREAK_TO_RAISE);
        message[0] = '\0';
        if (readsFailing) {
            snprintf(message, sizeof(message), "Sustained ADE7953 reading failures (%llu in the last window)",
                     failureDelta);
        }
        _updateInstance(IssueLogic::Code::Ade7953ReadFailures, ISSUE_GLOBAL_SCOPE, readsFailing, message);
    }

    // Instance table management
    // =========================================================

    static void _updateInstance(IssueLogic::Code code, uint8_t channel, bool conditionTrue, const char *message)
    {
        bool raised = false;
        bool cleared = false;
        char logMessage[ISSUE_MESSAGE_BUFFER_SIZE] = {0};

        if (!acquireMutex(&_registryMutex)) {
            LOG_ERROR("Failed to acquire registry mutex for update");
            return;
        }

        IssueInstance *inst = _findInstance(code, channel);
        if (inst == nullptr) {
            if (!conditionTrue) { // Nothing exists and nothing to raise
                releaseMutex(&_registryMutex);
                return;
            }
            inst = _allocateInstance();
            if (inst == nullptr) { // Table full - log outside the mutex
                releaseMutex(&_registryMutex);
                LOG_WARNING("Issue instance table full, dropping raise of %s", IssueLogic::codeToString(code));
                return;
            }
            inst->code = code;
            inst->channel = channel;
            inst->state = IssueLogic::State::Inactive;
            inst->occurrences = 0;
            inst->firstSeenUnix = (uint32_t)CustomTime::getUnixTime();
            inst->message[0] = '\0';
        }

        IssueLogic::Transition transition = IssueLogic::step(inst->state, conditionTrue);
        if (transition.state != inst->state) {
            inst->state = transition.state;
            inst->lastChangeUnix = (uint32_t)CustomTime::getUnixTime();
        }
        if (transition.raised) {
            inst->occurrences++;
            if (message != nullptr && message[0] != '\0') {
                snprintf(inst->message, sizeof(inst->message), "%s", message);
            }
            raised = true;
        }
        if (transition.cleared) cleared = true;
        if (raised || cleared) snprintf(logMessage, sizeof(logMessage), "%s", inst->message);
        if (inst->state == IssueLogic::State::Inactive) memset(inst, 0, sizeof(*inst));

        releaseMutex(&_registryMutex);

        if (raised) _logTransition("raised", true, code, channel, logMessage);
        if (cleared) _logTransition("cleared", false, code, channel, logMessage);
    }

    static IssueInstance* _findInstance(IssueLogic::Code code, uint8_t channel)
    {
        for (uint8_t i = 0; i < ISSUE_MAX_INSTANCES; i++) {
            if (!IssueLogic::isVisible(_instances[i].state)) continue;
            if (_instances[i].code == code && _instances[i].channel == channel) return &_instances[i];
        }
        return nullptr;
    }

    static IssueInstance* _allocateInstance()
    {
        for (uint8_t i = 0; i < ISSUE_MAX_INSTANCES; i++) {
            if (!IssueLogic::isVisible(_instances[i].state)) return &_instances[i];
        }
        return nullptr;
    }

    // Apply an acknowledgement to one instance. Caller must hold _registryMutex.
    static void _ackInstanceLocked(IssueInstance &inst)
    {
        inst.state = IssueLogic::applyAck(inst.state);
        inst.lastChangeUnix = (uint32_t)CustomTime::getUnixTime();
        if (inst.state == IssueLogic::State::Inactive) memset(&inst, 0, sizeof(inst));
    }

    static const char* _stateToString(IssueLogic::State state)
    {
        switch (state) {
            case IssueLogic::State::ActiveUnacked:  return "active_unacked";
            case IssueLogic::State::ActiveAcked:    return "active_acked";
            case IssueLogic::State::ClearedUnacked: return "cleared_unacked";
            default:                                return "inactive";
        }
    }

    static bool _codeFromString(const char *codeStr, IssueLogic::Code &code)
    {
        if (codeStr == nullptr) return false;
        for (uint8_t i = 0; i < (uint8_t)IssueLogic::Code::Count; i++) {
            if (strcmp(codeStr, IssueLogic::codeToString((IssueLogic::Code)i)) == 0) {
                code = (IssueLogic::Code)i;
                return true;
            }
        }
        return false;
    }

    // Uniform transition log line: the firmware log is the forensic record, so
    // every raise / clear / ack is grep-able as "issue <verb>: code=".
    static void _logTransition(const char *verb, bool raise, IssueLogic::Code code, uint8_t channel, const char *message)
    {
        char channelStr[8];
        if (channel == ISSUE_GLOBAL_SCOPE) snprintf(channelStr, sizeof(channelStr), "none");
        else snprintf(channelStr, sizeof(channelStr), "%u", channel);

        IssueLogic::Severity severity = IssueLogic::codeSeverity(code);
        char line[ISSUE_MESSAGE_BUFFER_SIZE + 96];
        snprintf(line, sizeof(line), "Issue %s: code=%s channel=%s severity=%s details=\"%s\"",
                 verb, IssueLogic::codeToString(code), channelStr,
                 IssueLogic::severityToString(severity), message);

        // Raise logs at the issue's own severity; clear/ack are informational
        if (raise && severity == IssueLogic::Severity::Error) LOG_ERROR("%s", line);
        else if (raise && severity == IssueLogic::Severity::Warning) LOG_WARNING("%s", line);
        else LOG_INFO("%s", line);
    }
}
