// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "structs.h"
#include "tunnel_logic.h"

// LAN tunnel (openspec capability: lan-tunnel). The cloud opens a session with
// the gateway command (op tunnel_open), then binary frames on
// energyme/<product>/v1/<thing>/tunnel/<sid>/{d,u} carry TCP channels to one
// private LAN host:port.
//
// Threading: the tunnel task owns every LAN socket, the ARQ and the timers. The
// MQTT task is the only one that subscribes, publishes and frees the session
// buffers: open(), pushDownlink(), takeUplink()/releaseUplink() and
// takeTerminal()/finish() are MQTT-task only. Everything else is safe from any task.

#define TUNNEL_ENABLED_KEY "tunnel"
#define TUNNEL_PORTS_KEY "tunnel_ports"
#define TUNNEL_ENABLED_DEFAULT false

#define TUNNEL_TASK_NAME "tunnel_task"
#define TUNNEL_TASK_STACK_SIZE (6 * 1024)
#define TUNNEL_TASK_PRIORITY 1
#define TUNNEL_IDLE_WAIT_MS 1000          // Idle task wakes this often to check for a stop request
#define TUNNEL_SELECT_MS 10               // Socket poll cap while a session is open
#define TUNNEL_COALESCE_MS 10             // Small LAN reads wait this long to fill a frame
#define TUNNEL_CONNECT_TIMEOUT_MS 5000
#define TUNNEL_WRITE_STALL_MS (120 * 1000)
#define TUNNEL_CLOSE_LINGER_MS 2000       // Wait this long for the CLOSED frame's ack
#define TUNNEL_ACK_DELAY_MS 40
#define TUNNEL_ACK_EVERY_FRAMES 2
#define TUNNEL_UPLINK_RATE_PER_S 25       // Shared with telemetry under AWS's 100 publishes/s per connection
#define TUNNEL_UPLINK_BURST 8
#define TUNNEL_CHANNEL_BUFFER (8 * 1024)  // Downlink ring per channel = advertised channel credit
#define TUNNEL_RX_POOL_SLOTS 20           // Inbound frames: queue + reorder (WINDOW)
#define TUNNEL_OUT_POOL_SLOTS 8           // Outbound frames waiting for the MQTT task
#define TUNNEL_RX_BATCH 16                // Inbound frames handled per task pass
#define TUNNEL_OPEN_MIN_FREE_HEAP (45 * 1024)
#define TUNNEL_OPEN_MIN_MAX_BLOCK (16 * 1024)
#define TUNNEL_LOW_HEAP (30 * 1024)
#define TUNNEL_KEEPALIVE_IDLE_S 60
#define TUNNEL_OTA_CLOSE_WAIT_MS 500
#define TUNNEL_SID_BUFFER_SIZE (TunnelLogic::SID_LENGTH + 1)
#define TUNNEL_IP_BUFFER_SIZE 16
#define TUNNEL_MQTT_UPLINK_BUDGET 4       // Frames the MQTT task publishes per loop iteration
#define TUNNEL_MQTT_RX_SPIN 8             // Extra loop() calls per iteration while tunnel frames are pending

namespace Tunnel
{
    void begin();
    void stop(); // Restart path: closes sockets, publishes nothing

    bool isEnabled(); // Own switch only; a session needs Gateway::isEnabled() too
    bool setEnabled(bool enabled); // Persists to NVS; false if not persisted

    // Port allowlist; count 0 means any port. setPorts() persists to NVS.
    size_t getPorts(uint16_t* ports, size_t capacity);
    bool setPorts(const uint16_t* ports, size_t count);

    struct ActiveInfo {
        char sid[TUNNEL_SID_BUFFER_SIZE];
        char ip[TUNNEL_IP_BUFFER_SIZE];
        uint16_t port;
        uint64_t since; // unix seconds
    };
    bool getActive(ActiveInfo* info); // false when no session is open
    bool isActive();                  // a session exists (open, closing or awaiting finish)

    enum class OpenResult { ACCEPTED, NOT_ENABLED, BUSY, BAD_ARGS, TARGET_NOT_PRIVATE, TARGET_NOT_ALLOWED, PORT_NOT_ALLOWED, NO_MEMORY };

    // `topicBase` is "energyme/<product>/v1/<thing>/tunnel". On ACCEPTED the
    // caller subscribes to downlinkTopic(). `description` gets a static string.
    OpenResult open(const char* executionId, JsonVariantConst args, const char* topicBase, const char** description);

    // tunnel_close: closes the session named by args.sid; false if none matches.
    bool closeFromCloud(JsonVariantConst args);

    // Any task: close the open session (no-op without one, first reason wins).
    void requestClose(TunnelLogic::CloseReason reason);

    // requestClose() then wait (bounded) until the LAN sockets are closed. The
    // CLOSED frame and terminal status follow from the MQTT task later.
    void closeAndWait(TunnelLogic::CloseReason reason, uint32_t timeoutMs);

    // Valid from open() until finish(); nullptr otherwise.
    const char* downlinkTopic();
    const char* uplinkTopic();

    // RX callback branch: true when `topic` is the session's downlink topic
    // (the frame is then queued or dropped), false for any other topic.
    bool pushDownlink(const char* topic, const uint8_t* payload, size_t length);

    // Next outbound frame; call releaseUplink() once published (or not).
    bool takeUplink(const uint8_t** frame, size_t* length);
    void releaseUplink();

    // Once the session has ended and its frames are drained: the execution id
    // and the PSRAM JSON command response (caller frees). Then unsubscribe and
    // call finish().
    bool takeTerminal(char* executionId, size_t executionIdSize, char** responseJson);
    void finish();

    void setCloudConnected(bool connected); // MQTT task, every loop iteration

    TaskInfo getTaskInfo();
}
