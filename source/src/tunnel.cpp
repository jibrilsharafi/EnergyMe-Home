// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "tunnel.h"

#include <atomic>

#include <AdvancedLogger.h>
#include <Preferences.h>

#include "lwip/sockets.h"

#include "constants.h"
#include "customtime.h"
#include "gateway.h"
#include "mqtt.h"
#include "shadow.h"
#include "structs.h"
#include "taskprofiler.h"
#include "utils.h"

using TunnelLogic::CloseReason;
using TunnelLogic::Header;
using TunnelLogic::Type;

namespace Tunnel
{
    static constexpr uint32_t NOTIFY_WORK_BIT = (1 << 1);
    static constexpr uint8_t NO_RST = 0xFF;

    enum class Phase : uint8_t { IDLE, ACTIVE, CLOSING, DONE };
    enum class ChState : uint8_t { FREE, CONNECTING, OPEN };

    struct Slot {
        uint16_t length;
        uint8_t data[TunnelLogic::FRAME_MAX];
    };

    struct RetxSlot {
        Type type;
        uint8_t channel;
        uint16_t length;
        uint8_t payload[TunnelLogic::DATA_MAX];
    };

    struct Channel {
        ChState state;
        int fd;
        uint32_t connectDeadlineMs;
        uint32_t readableSinceMs; // 0 = not waiting to coalesce
        uint32_t stallSinceMs;    // 0 = writes progressing
        TunnelLogic::ByteRing ring;
        TunnelLogic::ReceiveCredit in;
        TunnelLogic::SendCredit out;
        bool okDue;
        uint8_t errDue; // 0 = none, else TunnelLogic::ChannelError
        bool remoteFin;
        bool lanEof;
        bool finDue;
        bool finSent;
        bool shutWr;
    };

    // Persistent settings
    static bool _enabled = TUNNEL_ENABLED_DEFAULT; // aligned bool: atomic read on the S3
    static uint16_t _ports[TunnelLogic::PORTS_MAX];
    static size_t _portCount = 0;
    static SemaphoreHandle_t _stateMutex = nullptr; // setters and the active-info snapshot

    static TaskHandle_t _taskHandle = nullptr;
    static TaskHeartbeat _heartbeat;
    static std::atomic<Phase> _phase{Phase::IDLE};
    static std::atomic<uint8_t> _closeRequest{0}; // CloseReason, first one wins
    static std::atomic<bool> _cloudConnected{false};

    // Session identity (written by open() before the phase leaves IDLE)
    static char _executionId[NAME_BUFFER_SIZE];
    static char _downTopic[MQTT_TOPIC_BUFFER_SIZE];
    static char _upTopic[MQTT_TOPIC_BUFFER_SIZE];
    static ActiveInfo _info;
    static uint8_t _target[4];
    static uint32_t _maxDurationS = 0;
    static uint32_t _idleS = 0;

    // PSRAM, allocated by open(), freed by finish()
    static Slot* _rxPool = nullptr;
    static Slot* _outPool = nullptr;
    static RetxSlot* _retx = nullptr;
    static uint8_t* _rings = nullptr;

    // Slot index queues (internal RAM, created once)
    static QueueHandle_t _rxFree = nullptr;
    static QueueHandle_t _rxReady = nullptr;
    static QueueHandle_t _outFree = nullptr;
    static QueueHandle_t _outReady = nullptr;
    static int _uplinkTaken = -1; // MQTT task only

    // Tunnel task state
    static Channel _channels[TunnelLogic::CHANNELS_MAX];
    static uint8_t _rstDue[TunnelLogic::CHANNELS_MAX + 1];
    static int _reorder[TunnelLogic::WINDOW]; // ArqReceiver slot -> rx pool index
    static TunnelLogic::ArqSender _sender;
    static TunnelLogic::ArqReceiver _receiver;
    static TunnelLogic::TokenBucket _bucket;
    static bool _openedDue = false;
    static bool _closedDue = false;
    static bool _pongDue = false;
    static bool _ackNow = false;
    static uint8_t _unacked = 0;          // sequenced frames received since our last ack
    static uint32_t _unackedSinceMs = 0;
    static CloseReason _closeReason = CloseReason::NONE;
    static uint32_t _closingSinceMs = 0;
    static bool _wasConnected = false;
    static TunnelLogic::SessionState _clock;
    static char* _terminalJson = nullptr;

    struct Counters {
        uint64_t bytesUp;
        uint64_t bytesDown;
        uint32_t channelsOpened;
        uint32_t framesRetx;
        uint32_t framesDropped;
    };
    static Counters _counters;
    static std::atomic<uint32_t> _rxDropped{0}; // counted by the MQTT task

    // Settings
    // ========

    static void _loadSettings() {
        Preferences preferences;
        _portCount = sizeof(TunnelLogic::DEFAULT_PORTS) / sizeof(TunnelLogic::DEFAULT_PORTS[0]);
        memcpy(_ports, TunnelLogic::DEFAULT_PORTS, sizeof(TunnelLogic::DEFAULT_PORTS));
        if (!preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, true)) return;
        _enabled = preferences.getBool(TUNNEL_ENABLED_KEY, TUNNEL_ENABLED_DEFAULT);
        if (preferences.isKey(TUNNEL_PORTS_KEY)) {
            size_t length = preferences.getBytesLength(TUNNEL_PORTS_KEY);
            if (length <= sizeof(_ports) && length % sizeof(uint16_t) == 0) {
                _portCount = preferences.getBytes(TUNNEL_PORTS_KEY, _ports, sizeof(_ports)) / sizeof(uint16_t);
                if (_portCount == 1 && _ports[0] == 0) _portCount = 0; // stored "any port" marker
            }
        }
        preferences.end();
    }

    bool isEnabled() { return _enabled; }

    bool setEnabled(bool enabled) {
        if (!createMutexIfNeeded(&_stateMutex) || !acquireMutex(&_stateMutex)) return false;
        Preferences preferences;
        bool persisted = preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, false) && preferences.putBool(TUNNEL_ENABLED_KEY, enabled) > 0;
        preferences.end();
        if (persisted) {
            if (_enabled != enabled) LOG_INFO("LAN tunnel %s", enabled ? "enabled" : "disabled");
            _enabled = enabled;
        } else {
            LOG_ERROR("Failed to persist the LAN tunnel switch");
        }
        releaseMutex(&_stateMutex);
        if (_taskHandle != nullptr) xTaskNotify(_taskHandle, NOTIFY_WORK_BIT, eSetBits);
        return persisted;
    }

    size_t getPorts(uint16_t* ports, size_t capacity) {
        if (!createMutexIfNeeded(&_stateMutex) || !acquireMutex(&_stateMutex)) return 0;
        size_t count = _portCount < capacity ? _portCount : capacity;
        memcpy(ports, _ports, count * sizeof(uint16_t));
        releaseMutex(&_stateMutex);
        return count;
    }

    bool setPorts(const uint16_t* ports, size_t count) {
        if (count > TunnelLogic::PORTS_MAX) return false;
        for (size_t i = 0; i < count; i++) {
            if (ports[i] == 0) return false;
        }
        if (!createMutexIfNeeded(&_stateMutex) || !acquireMutex(&_stateMutex)) return false;
        Preferences preferences;
        bool persisted = preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, false);
        // putBytes refuses an empty blob, so "any port" is stored as the single port 0.
        static const uint16_t ANY_PORT_MARKER = 0;
        const uint16_t* stored = count == 0 ? &ANY_PORT_MARKER : ports;
        size_t storedBytes = (count == 0 ? 1 : count) * sizeof(uint16_t);
        if (persisted) persisted = preferences.putBytes(TUNNEL_PORTS_KEY, stored, storedBytes) == storedBytes;
        preferences.end();
        if (persisted) {
            memcpy(_ports, ports, count * sizeof(uint16_t));
            _portCount = count;
            LOG_INFO("LAN tunnel port allowlist set (%u ports%s)", (unsigned)count, count == 0 ? ", any port" : "");
        } else {
            LOG_ERROR("Failed to persist the LAN tunnel port allowlist");
        }
        releaseMutex(&_stateMutex);
        return persisted;
    }

    bool isActive() { return _phase.load() != Phase::IDLE; }

    bool getActive(ActiveInfo* info) {
        Phase phase = _phase.load();
        if (phase != Phase::ACTIVE && phase != Phase::CLOSING) return false;
        if (!acquireMutex(&_stateMutex)) return false;
        *info = _info;
        releaseMutex(&_stateMutex);
        return true;
    }

    // Start time for the "...SinceMs" fields, where 0 means "not started". Never
    // `now | 1`: on an even millisecond that is now + 1 and `now - since` wraps.
    static uint32_t _stamp(uint32_t now) { return now != 0 ? now : 1; }

    // Frame emission (tunnel task)
    // ============================

    static int _freeOutSlot() {
        uint8_t index;
        return xQueueReceive(_outFree, &index, 0) == pdTRUE ? index : -1;
    }

    static void _queueOut(uint8_t index) {
        xQueueSend(_outReady, &index, 0); // capacity == pool size, never full
        _unacked = 0;
        _unackedSinceMs = 0;
        _ackNow = false;
        Mqtt::requestImmediatePublish();
    }

    // Room for one more outbound frame: a free slot and a rate token. The slot is
    // claimed here so the token is never wasted.
    static int _claimOut(uint32_t now) {
        if (uxQueueMessagesWaiting(_outFree) == 0) return -1;
        if (!_bucket.take(now)) return -1;
        return _freeOutSlot();
    }

    static Header _header(Type type, uint8_t channel, uint32_t seq, uint8_t flags) {
        return {TunnelLogic::VERSION, type, channel, (uint8_t)(flags | TunnelLogic::FLAG_ACK_VALID), seq, _receiver.ack(), _receiver.sack(), 0};
    }

    static bool _canSendSequenced() { return _sender.canSend() && uxQueueMessagesWaiting(_outFree) > 0; }

    static void _releaseOut(int index) {
        uint8_t i = (uint8_t)index;
        xQueueSend(_outFree, &i, 0);
    }

    // Copies the payload into the retransmit ring, then into outbound slot `out`
    // (claimed here when negative).
    static bool _sendSequenced(Type type, uint8_t channel, const uint8_t* payload, size_t length, uint32_t now, int out = -1) {
        if (!_sender.canSend()) {
            if (out >= 0) _releaseOut(out);
            return false;
        }
        if (out < 0) out = _claimOut(now);
        if (out < 0) return false;
        uint8_t slot;
        uint32_t seq = _sender.allocate(now, &slot);
        RetxSlot& r = _retx[slot];
        r.type = type;
        r.channel = channel;
        r.length = (uint16_t)length;
        if (length > 0 && payload != r.payload) memcpy(r.payload, payload, length);
        _outPool[out].length = (uint16_t)TunnelLogic::buildFrame(_outPool[out].data, sizeof(_outPool[out].data), _header(type, channel, seq, 0), r.payload, length);
        _queueOut((uint8_t)out);
        return true;
    }

    static bool _sendUnsequenced(Type type, uint32_t now) {
        int out = _claimOut(now);
        if (out < 0) return false;
        _outPool[out].length = (uint16_t)TunnelLogic::buildFrame(_outPool[out].data, sizeof(_outPool[out].data), _header(type, 0, 0, 0), nullptr, 0);
        _queueOut((uint8_t)out);
        return true;
    }

    static bool _retransmit(uint8_t slot, uint32_t now) {
        int out = _claimOut(now);
        if (out < 0) return false;
        const RetxSlot& r = _retx[slot];
        _outPool[out].length = (uint16_t)TunnelLogic::buildFrame(_outPool[out].data, sizeof(_outPool[out].data),
                                                                 _header(r.type, r.channel, _sender.seqOf(slot), TunnelLogic::FLAG_RETX), r.payload, r.length);
        _queueOut((uint8_t)out);
        _counters.framesRetx++;
        return true;
    }

    // Channels (tunnel task)
    // ======================

    static Channel& _ch(uint8_t id) { return _channels[id - 1]; }

    // abort: RST semantics towards the LAN host (linger 0). rst: tell the operator.
    static void _closeChannel(uint8_t id, bool abort, uint8_t rstReason) {
        Channel& c = _ch(id);
        if (c.fd >= 0) {
            if (abort) {
                struct linger lin = {1, 0};
                setsockopt(c.fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
            }
            close(c.fd);
        }
        c = {};
        c.fd = -1;
        if (rstReason != NO_RST) _rstDue[id] = rstReason;
    }

    static void _openChannel(uint8_t id, uint32_t credit, uint32_t now) {
        Channel& c = _ch(id);
        // Both sides sent FIN and the operator saw every byte drained (that is its
        // reuse rule): the old channel is only waiting for _serviceChannels to free it.
        if (c.state != ChState::FREE && c.remoteFin && c.finSent && c.ring.size() == 0) _closeChannel(id, false, NO_RST);
        if (c.state != ChState::FREE) {
            _closeChannel(id, true, NO_RST);
            _ch(id).errDue = (uint8_t)TunnelLogic::ChannelError::BUSY;
            return;
        }
        _rstDue[id] = NO_RST;
        c.errDue = 0; // a failed open earlier in the same batch must not answer this one
        c.okDue = false;
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd < 0) {
            c.errDue = (uint8_t)TunnelLogic::ChannelError::NO_SOCKET;
            return;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#ifdef TCP_KEEPIDLE
        int idle = TUNNEL_KEEPALIVE_IDLE_S;
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif

        struct sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(_info.port);
        memcpy(&address.sin_addr.s_addr, _target, 4);
        if (connect(fd, (struct sockaddr*)&address, sizeof(address)) < 0 && errno != EINPROGRESS) {
            int error = errno;
            close(fd);
            c.errDue = (uint8_t)(error == ECONNREFUSED ? TunnelLogic::ChannelError::REFUSED : TunnelLogic::ChannelError::CONNECT_FAILED);
            return;
        }
        c.fd = fd;
        c.state = ChState::CONNECTING;
        c.connectDeadlineMs = now + TUNNEL_CONNECT_TIMEOUT_MS;
        c.ring.init(_rings + (id - 1) * TUNNEL_CHANNEL_BUFFER, TUNNEL_CHANNEL_BUFFER);
        c.in.reset(TUNNEL_CHANNEL_BUFFER);
        c.out.onGrant(credit);
        _counters.channelsOpened++;
    }

    static void _finishConnect(uint8_t id) {
        Channel& c = _ch(id);
        int error = 0;
        socklen_t length = sizeof(error);
        getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &error, &length);
        if (error == 0) {
            c.state = ChState::OPEN;
            c.okDue = true;
            return;
        }
        uint8_t reason = (uint8_t)(error == ECONNREFUSED ? TunnelLogic::ChannelError::REFUSED : TunnelLogic::ChannelError::CONNECT_FAILED);
        _closeChannel(id, false, NO_RST);
        _ch(id).errDue = reason;
    }

    static void _writeChannel(uint8_t id, uint32_t now) {
        Channel& c = _ch(id);
        size_t loops = 0;
        while (c.ring.size() > 0 && loops++ < 4) {
            const uint8_t* data;
            size_t run = c.ring.peek(&data);
            ssize_t sent = send(c.fd, data, run, MSG_DONTWAIT);
            if (sent > 0) {
                c.ring.consume((size_t)sent);
                c.in.onDrained((uint32_t)sent);
                c.stallSinceMs = 0;
                continue;
            }
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (c.stallSinceMs == 0) c.stallSinceMs = _stamp(now);
                return;
            }
            _closeChannel(id, true, (uint8_t)TunnelLogic::RstReason::LAN_RESET);
            return;
        }
    }

    // Watch a socket for reading only when a frame could go out right now: credit,
    // window, a free slot and a rate token, and not inside a coalescing wait.
    // Otherwise select() would return at once and the task would spin.
    static bool _canRead(const Channel& c, uint32_t now) {
        bool coalescing = c.readableSinceMs != 0 && TunnelLogic::elapsedMs(now, c.readableSinceMs) < TUNNEL_COALESCE_MS;
        return c.state == ChState::OPEN && !c.okDue && !c.lanEof && c.out.available() > 0 && !coalescing &&
               _canSendSequenced() && _bucket.ready(now);
    }

    static void _readChannel(uint8_t id, uint32_t now) {
        Channel& c = _ch(id);
        int available = 0;
        ioctl(c.fd, FIONREAD, &available);
        size_t limit = c.out.available() < TunnelLogic::DATA_MAX ? c.out.available() : TunnelLogic::DATA_MAX;
        if (c.readableSinceMs == 0) c.readableSinceMs = _stamp(now);
        if (!TunnelLogic::shouldReadNow(available > 0 ? (size_t)available : 0, limit, c.readableSinceMs, now, TUNNEL_COALESCE_MS)) return;
        // Slot and rate token first: bytes taken out of lwIP must go out in a frame.
        int out = _claimOut(now);
        if (out < 0) return;
        c.readableSinceMs = 0;

        // Straight into the retransmit slot the next seq will use (free: canSend()).
        uint8_t* buffer = _retx[_sender.nextSeq() % TunnelLogic::WINDOW].payload;
        ssize_t received = recv(c.fd, buffer, limit, MSG_DONTWAIT);
        if (received <= 0) _releaseOut(out);
        if (received > 0) {
            _sendSequenced(Type::DATA, id, buffer, (size_t)received, now, out); // canSend() checked by _canRead
            c.out.consume((uint32_t)received);
            _counters.bytesUp += (uint64_t)received;
            _clock.lastDataMs = now;
            return;
        }
        if (received == 0) {
            c.lanEof = true;
            c.finDue = true;
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        _closeChannel(id, true, (uint8_t)TunnelLogic::RstReason::LAN_RESET);
    }

    static void _pollSockets(uint32_t now) {
        fd_set readSet, writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        int maxFd = -1;
        for (uint8_t id = 1; id <= TunnelLogic::CHANNELS_MAX; id++) {
            Channel& c = _ch(id);
            if (c.state == ChState::FREE) continue;
            bool watch = false;
            if (c.state == ChState::CONNECTING || (c.ring.size() > 0 && !c.shutWr)) {
                FD_SET(c.fd, &writeSet);
                watch = true;
            }
            if (_canRead(c, now)) {
                FD_SET(c.fd, &readSet);
                watch = true;
            }
            if (watch && c.fd > maxFd) maxFd = c.fd;
        }

        if (maxFd < 0) {
            uint32_t bits = 0;
            xTaskNotifyWait(0, NOTIFY_WORK_BIT, &bits, pdMS_TO_TICKS(TUNNEL_SELECT_MS)); // shutdown bit stays for the task loop
            return;
        }
        struct timeval timeout = {0, TUNNEL_SELECT_MS * 1000};
        int ready = select(maxFd + 1, &readSet, &writeSet, nullptr, &timeout);
        now = millis();
        if (ready <= 0) return;
        for (uint8_t id = 1; id <= TunnelLogic::CHANNELS_MAX; id++) {
            Channel& c = _ch(id);
            if (c.state == ChState::FREE) continue;
            int fd = c.fd;
            if (FD_ISSET(fd, &writeSet)) {
                if (c.state == ChState::CONNECTING) _finishConnect(id);
                else _writeChannel(id, now);
            }
            if (_ch(id).state != ChState::FREE && _ch(id).fd == fd && FD_ISSET(fd, &readSet) && _canRead(_ch(id), now)) _readChannel(id, now);
        }
    }

    // Per-channel housekeeping: connect deadline, write stall, half-close, free.
    static void _serviceChannels(uint32_t now) {
        for (uint8_t id = 1; id <= TunnelLogic::CHANNELS_MAX; id++) {
            Channel& c = _ch(id);
            if (c.state == ChState::FREE) continue;
            if (c.state == ChState::CONNECTING && (int32_t)(now - c.connectDeadlineMs) >= 0) {
                _closeChannel(id, true, NO_RST);
                _ch(id).errDue = (uint8_t)TunnelLogic::ChannelError::TIMEOUT;
                continue;
            }
            // Started here, not only on EAGAIN: lwIP may never report the socket
            // writable again, and then _writeChannel never runs.
            if (c.ring.size() > 0 && c.stallSinceMs == 0) c.stallSinceMs = _stamp(now);
            if (c.ring.size() == 0) c.stallSinceMs = 0;
            if (c.ring.size() > 0 && TunnelLogic::elapsedMs(now, c.stallSinceMs) >= TUNNEL_WRITE_STALL_MS) {
                _closeChannel(id, true, (uint8_t)TunnelLogic::RstReason::WRITE_STALL);
                continue;
            }
            if (c.state == ChState::OPEN && c.remoteFin && !c.shutWr && c.ring.size() == 0) {
                shutdown(c.fd, SHUT_WR);
                c.shutWr = true;
            }
            if (c.finSent && c.shutWr && c.ring.size() == 0 && !c.in.shouldAdvertise()) _closeChannel(id, false, NO_RST);
        }
    }

    // Downlink (tunnel task)
    // ======================

    static void _deliver(const Header& h, const uint8_t* payload, uint32_t now) {
        uint8_t id = h.channel;
        switch (h.type) {
            case Type::DATA: {
                Channel& c = _ch(id);
                if (c.state == ChState::FREE) {
                    _rstDue[id] = (uint8_t)TunnelLogic::RstReason::NOT_OPEN;
                    return;
                }
                if (c.remoteFin) {
                    _closeChannel(id, true, (uint8_t)TunnelLogic::RstReason::PROTOCOL);
                    return;
                }
                if (!c.in.accepts(h.length)) {
                    _closeChannel(id, true, (uint8_t)TunnelLogic::RstReason::PROTOCOL);
                    return;
                }
                c.ring.write(payload, h.length);
                c.in.onReceived(h.length);
                _counters.bytesDown += h.length;
                _clock.lastDataMs = now;
                return;
            }
            case Type::CH_OPEN: _openChannel(id, TunnelLogic::getU32(payload), now); return;
            case Type::CH_CREDIT:
                if (_ch(id).state != ChState::FREE) _ch(id).out.onGrant(TunnelLogic::getU32(payload));
                return;
            case Type::FIN:
                if (_ch(id).state == ChState::FREE) _rstDue[id] = (uint8_t)TunnelLogic::RstReason::NOT_OPEN;
                else _ch(id).remoteFin = true;
                return;
            case Type::RST:
                if (_ch(id).state != ChState::FREE) _closeChannel(id, true, NO_RST);
                return;
            case Type::CLOSE: requestClose(CloseReason::OPERATOR_CLOSED); return;
            default: return; // CH_OK/CH_ERR/OPENED/CLOSED from the operator: meaningless, ignored
        }
    }

    static void _releaseRx(int index) {
        uint8_t i = (uint8_t)index;
        xQueueSend(_rxFree, &i, 0);
    }

    static void _handleFrame(int index, uint32_t now) {
        Slot& slot = _rxPool[index];
        Header h;
        if (TunnelLogic::decodeFrame(slot.data, slot.length, &h) != TunnelLogic::Decode::OK) {
            _counters.framesDropped++;
            _releaseRx(index);
            return;
        }
        _clock.lastDownlinkMs = now;
        if (h.flags & TunnelLogic::FLAG_ACK_VALID) _sender.onAck(h.ack, h.sack, now);

        if (!TunnelLogic::isSequenced(h.type)) {
            if (h.type == Type::PING) _pongDue = true;
            _releaseRx(index);
            return;
        }

        uint8_t reorderSlot = 0;
        switch (_receiver.onFrame(h.seq, &reorderSlot)) {
            case TunnelLogic::ArqReceiver::Verdict::DELIVER: {
                if (_unacked++ == 0) _unackedSinceMs = _stamp(now);
                if (_phase.load() == Phase::ACTIVE) _deliver(h, slot.data + TunnelLogic::HEADER_SIZE, now);
                _releaseRx(index);
                size_t loops = 0;
                while (_receiver.popReady(&reorderSlot) && loops++ < TunnelLogic::WINDOW) {
                    int buffered = _reorder[reorderSlot];
                    _reorder[reorderSlot] = -1;
                    Header bh;
                    TunnelLogic::decodeFrame(_rxPool[buffered].data, _rxPool[buffered].length, &bh);
                    if (_phase.load() == Phase::ACTIVE) _deliver(bh, _rxPool[buffered].data + TunnelLogic::HEADER_SIZE, now);
                    _releaseRx(buffered);
                    _unacked++;
                }
                return;
            }
            case TunnelLogic::ArqReceiver::Verdict::BUFFER:
                _reorder[reorderSlot] = index;
                _ackNow = true; // gap: tell the sender at once (SACK)
                return;
            default:
                // Duplicate (our ack was lost) or beyond the window: re-ack now.
                _ackNow = true;
                _releaseRx(index);
                return;
        }
    }

    // Uplink scheduling (tunnel task)
    // ===============================

    static void _emit(uint32_t now) {
        uint8_t slot;
        bool timeout;
        size_t loops = 0;
        while (_cloudConnected.load() && _sender.nextRetransmit(now, &slot, &timeout) && loops++ < TunnelLogic::WINDOW) {
            if (!_retransmit(slot, now)) break;
            _sender.markRetransmitted(slot, now, timeout);
        }

        if (_openedDue && _canSendSequenced()) {
            TunnelLogic::Opened opened = {(uint16_t)TunnelLogic::DATA_MAX, TunnelLogic::WINDOW, TunnelLogic::CHANNELS_MAX,
                                          TUNNEL_CHANNEL_BUFFER, _maxDurationS, _idleS};
            uint8_t payload[TunnelLogic::OPENED_SIZE];
            TunnelLogic::encodeOpened(opened, payload);
            if (_sendSequenced(Type::OPENED, 0, payload, sizeof(payload), now)) _openedDue = false;
        }

        for (uint8_t id = 1; id <= TunnelLogic::CHANNELS_MAX; id++) {
            Channel& c = _ch(id);
            if (_rstDue[id] != NO_RST && _canSendSequenced()) {
                if (_sendSequenced(Type::RST, id, &_rstDue[id], 1, now)) _rstDue[id] = NO_RST;
            }
            if (c.errDue != 0 && _canSendSequenced()) {
                if (_sendSequenced(Type::CH_ERR, id, &c.errDue, 1, now)) c.errDue = 0;
            }
            if (c.okDue && _canSendSequenced()) {
                if (_sendSequenced(Type::CH_OK, id, nullptr, 0, now)) c.okDue = false;
            }
            if (c.state != ChState::FREE && c.in.shouldAdvertise() && _canSendSequenced()) {
                uint8_t payload[4];
                TunnelLogic::putU32(payload, c.in.grant());
                if (_sendSequenced(Type::CH_CREDIT, id, payload, sizeof(payload), now)) c.in.advertised = c.in.grant();
            }
            if (c.finDue && _canSendSequenced()) {
                if (_sendSequenced(Type::FIN, id, nullptr, 0, now)) {
                    c.finDue = false;
                    c.finSent = true;
                }
            }
        }

        if (_closedDue && _canSendSequenced()) {
            uint8_t reason = (uint8_t)_closeReason;
            if (_sendSequenced(Type::CLOSED, 0, &reason, 1, now)) _closedDue = false;
        }

        if (_pongDue && _sendUnsequenced(Type::PONG, now)) _pongDue = false;

        bool ackDue = _ackNow || _unacked >= TUNNEL_ACK_EVERY_FRAMES ||
                      (_unacked > 0 && TunnelLogic::elapsedMs(now, _unackedSinceMs) >= TUNNEL_ACK_DELAY_MS);
        if (ackDue) _sendUnsequenced(Type::ACK, now);
    }

    // Session lifecycle (tunnel task)
    // ===============================

    static void _buildTerminal(uint32_t now) {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        bool succeeded = TunnelLogic::closeReasonSucceeded(_closeReason);
        const char* reason = TunnelLogic::closeReasonName(_closeReason);
        doc["status"] = succeeded ? "SUCCEEDED" : "FAILED";
        doc["statusReason"]["reasonCode"] = reason;
        JsonObject result = doc["result"].to<JsonObject>();
        char buffer[24];
        result["sid"]["s"] = _info.sid;
        result["reason"]["s"] = reason;
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)((now - _clock.openedMs) / 1000));
        result["duration_s"]["s"] = buffer;
        snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)_counters.bytesUp);
        result["bytes_up"]["s"] = buffer;
        snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)_counters.bytesDown);
        result["bytes_down"]["s"] = buffer;
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)_counters.channelsOpened);
        result["channels_opened"]["s"] = buffer;
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)_counters.framesRetx);
        result["frames_retx"]["s"] = buffer;
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)(_counters.framesDropped + _rxDropped.load()));
        result["frames_dropped"]["s"] = buffer;

        size_t length = measureJson(doc) + 1;
        _terminalJson = (char*)ps_malloc(length);
        if (_terminalJson != nullptr) serializeJson(doc, _terminalJson, length);

        if (succeeded) {
            LOG_INFO("Tunnel %s closed: %s after %lu s, up %llu B, down %llu B, %lu channels, %lu retx, %lu dropped",
                     _executionId, reason, (unsigned long)((now - _clock.openedMs) / 1000), (unsigned long long)_counters.bytesUp,
                     (unsigned long long)_counters.bytesDown, (unsigned long)_counters.channelsOpened,
                     (unsigned long)_counters.framesRetx, (unsigned long)(_counters.framesDropped + _rxDropped.load()));
        } else {
            LOG_WARNING("Tunnel %s closed: %s after %lu s, up %llu B, down %llu B, %lu channels, %lu retx, %lu dropped",
                        _executionId, reason, (unsigned long)((now - _clock.openedMs) / 1000), (unsigned long long)_counters.bytesUp,
                        (unsigned long long)_counters.bytesDown, (unsigned long)_counters.channelsOpened,
                        (unsigned long)_counters.framesRetx, (unsigned long)(_counters.framesDropped + _rxDropped.load()));
        }
    }

    static void _closeAllChannels(bool abort) {
        for (uint8_t id = 1; id <= TunnelLogic::CHANNELS_MAX; id++) {
            if (_ch(id).state != ChState::FREE) _closeChannel(id, abort, NO_RST);
            _rstDue[id] = NO_RST;
        }
    }

    static void _beginClose(CloseReason reason, uint32_t now) {
        _closeReason = reason;
        _closeAllChannels(reason != CloseReason::OPERATOR_CLOSED && reason != CloseReason::CLOUD_CLOSED &&
                          reason != CloseReason::IDLE_TIMEOUT && reason != CloseReason::MAX_DURATION);
        _openedDue = false;
        _closedDue = true;
        _closingSinceMs = now;
        _phase.store(Phase::CLOSING);
    }

    // True once the session is DONE: the MQTT task may free and reuse everything
    // from then on, so the caller must not touch session state again.
    static bool _serviceSession(uint32_t now) {
        bool connected = _cloudConnected.load();
        if (connected && !_wasConnected) {
            // Back online: nothing sent while down arrived, resend it all now.
            _sender.rearmAll();
            _clock.lastDownlinkMs = now;
            _clock.disconnectedSinceMs = 0;
        } else if (!connected && _wasConnected) {
            _clock.disconnectedSinceMs = _stamp(now);
        }
        _wasConnected = connected;

        if (_phase.load() == Phase::CLOSING) {
            bool acked = !_closedDue && _sender.inFlight() == 0;
            if (acked || now - _closingSinceMs >= TUNNEL_CLOSE_LINGER_MS) {
                _closeAllChannels(true);
                _buildTerminal(now);
                _phase.store(Phase::DONE);
                return true;
            }
            return false;
        }

        size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (freeHeap < TUNNEL_LOW_HEAP) {
            if (_clock.lowHeapSinceMs == 0) _clock.lowHeapSinceMs = _stamp(now);
        } else {
            _clock.lowHeapSinceMs = 0;
        }
        _clock.enabled = _enabled && Gateway::isEnabled();
        _clock.cloudConnected = connected;
        _clock.consecutiveTimeouts = connected ? _sender.consecutiveTimeouts() : 0;
        _clock.channelBytes = _counters.bytesUp + _counters.bytesDown;

        CloseReason reason = (CloseReason)_closeRequest.load();
        if (reason == CloseReason::NONE) reason = TunnelLogic::checkClose(_clock, now);
        if (reason != CloseReason::NONE) _beginClose(reason, now);
        return false;
    }

    static void _runSession() {
        uint32_t now = millis();
        size_t loops = 0;
        uint8_t index;
        while (loops++ < TUNNEL_RX_BATCH && xQueueReceive(_rxReady, &index, 0) == pdTRUE) _handleFrame(index, now);

        if (_serviceSession(now)) return;
        if (_phase.load() == Phase::ACTIVE) _serviceChannels(now);
        _emit(now);
        _pollSockets(now);
    }

    static void _tunnelTask(void* parameter) {
        (void)parameter;
        LOG_DEBUG("Tunnel task started");
        bool shouldRun = true;
        while (shouldRun) {
            TASK_HEARTBEAT(_heartbeat);
            Phase phase = _phase.load();
            if (phase == Phase::ACTIVE || phase == Phase::CLOSING) {
                _runSession();
                uint32_t bits = 0;
                xTaskNotifyWait(0, TASK_NOTIFY_SHUTDOWN_BIT | NOTIFY_WORK_BIT, &bits, 0);
                if (bits & TASK_NOTIFY_SHUTDOWN_BIT) shouldRun = false;
            } else {
                uint32_t bits = 0;
                xTaskNotifyWait(0, TASK_NOTIFY_SHUTDOWN_BIT | NOTIFY_WORK_BIT, &bits, pdMS_TO_TICKS(TUNNEL_IDLE_WAIT_MS));
                if (bits & TASK_NOTIFY_SHUTDOWN_BIT) shouldRun = false;
            }
        }
        if (_phase.load() == Phase::ACTIVE || _phase.load() == Phase::CLOSING) _closeAllChannels(true);
        LOG_DEBUG("Tunnel task stopping");
        _taskHandle = nullptr;
        vTaskDelete(NULL);
    }

    // Public API
    // ==========

    void begin() {
        _loadSettings();
        if (!createMutexIfNeeded(&_stateMutex)) {
            LOG_ERROR("Failed to create the tunnel mutex");
            return;
        }
        if (_rxFree == nullptr) _rxFree = xQueueCreate(TUNNEL_RX_POOL_SLOTS, sizeof(uint8_t));
        if (_rxReady == nullptr) _rxReady = xQueueCreate(TUNNEL_RX_POOL_SLOTS, sizeof(uint8_t));
        if (_outFree == nullptr) _outFree = xQueueCreate(TUNNEL_OUT_POOL_SLOTS, sizeof(uint8_t));
        if (_outReady == nullptr) _outReady = xQueueCreate(TUNNEL_OUT_POOL_SLOTS, sizeof(uint8_t));
        if (_rxFree == nullptr || _rxReady == nullptr || _outFree == nullptr || _outReady == nullptr) {
            LOG_ERROR("Failed to create the tunnel queues");
            return;
        }
        for (auto& c : _channels) c.fd = -1;
        if (_taskHandle == nullptr &&
            xTaskCreate(_tunnelTask, TUNNEL_TASK_NAME, TUNNEL_TASK_STACK_SIZE, nullptr, TUNNEL_TASK_PRIORITY, &_taskHandle) != pdPASS) {
            LOG_ERROR("Failed to create the tunnel task");
            _taskHandle = nullptr;
            return;
        }
        LOG_INFO("LAN tunnel ready (%s)", _enabled ? "enabled" : "disabled");
    }

    void stop() { stopTaskGracefully(&_taskHandle, "tunnel task"); }

    static void _freeBuffers() {
        free(_rxPool);
        free(_outPool);
        free(_retx);
        free(_rings);
        _rxPool = _outPool = nullptr;
        _retx = nullptr;
        _rings = nullptr;
    }

    static bool _allocateBuffers() {
        _rxPool = (Slot*)ps_malloc(sizeof(Slot) * TUNNEL_RX_POOL_SLOTS);
        _outPool = (Slot*)ps_malloc(sizeof(Slot) * TUNNEL_OUT_POOL_SLOTS);
        _retx = (RetxSlot*)ps_malloc(sizeof(RetxSlot) * TunnelLogic::WINDOW);
        _rings = (uint8_t*)ps_malloc(TUNNEL_CHANNEL_BUFFER * TunnelLogic::CHANNELS_MAX);
        if (_rxPool == nullptr || _outPool == nullptr || _retx == nullptr || _rings == nullptr) {
            _freeBuffers();
            return false;
        }
        xQueueReset(_rxFree);
        xQueueReset(_rxReady);
        xQueueReset(_outFree);
        xQueueReset(_outReady);
        for (uint8_t i = 0; i < TUNNEL_RX_POOL_SLOTS; i++) xQueueSend(_rxFree, &i, 0);
        for (uint8_t i = 0; i < TUNNEL_OUT_POOL_SLOTS; i++) xQueueSend(_outFree, &i, 0);
        return true;
    }

    static void _resetSessionState(uint32_t now) {
        for (uint8_t id = 1; id <= TunnelLogic::CHANNELS_MAX; id++) {
            _channels[id - 1] = {};
            _channels[id - 1].fd = -1;
            _rstDue[id] = NO_RST;
        }
        for (auto& r : _reorder) r = -1;
        _sender.reset();
        _receiver.reset();
        _bucket.reset(TUNNEL_UPLINK_RATE_PER_S, TUNNEL_UPLINK_BURST, now);
        _openedDue = true;
        _closedDue = _pongDue = _ackNow = false;
        _unacked = 0;
        _unackedSinceMs = 0;
        _closeReason = CloseReason::NONE;
        _closeRequest.store(0);
        _wasConnected = _cloudConnected.load();
        _counters = {};
        _rxDropped.store(0);
        _uplinkTaken = -1;
        _clock = {};
        _clock.openedMs = _clock.lastDownlinkMs = _clock.lastDataMs = now;
        _clock.maxDurationMs = _maxDurationS * 1000;
        _clock.idleMs = _idleS * 1000;
    }

    static bool _readArgsInt(JsonObjectConst args, const char* key, int64_t* out) {
        JsonVariantConst value = args[key];
        if (value.isNull()) {
            *out = 0;
            return true;
        }
        if (!value.is<int64_t>()) return false;
        *out = value.as<int64_t>();
        return true;
    }

    OpenResult open(const char* executionId, JsonVariantConst rawArgs, const char* topicBase, const char** description) {
        *description = nullptr;
        if (!_enabled || !Gateway::isEnabled() || _taskHandle == nullptr) {
            *description = "Enable the gateway and the tunnel (system shadow gateway_enabled, tunnel_enabled)";
            return OpenResult::NOT_ENABLED;
        }
        if (_phase.load() != Phase::IDLE) {
            *description = "A tunnel session is already open";
            return OpenResult::BUSY;
        }

        SpiRamAllocator allocator;
        JsonDocument argsDoc(&allocator);
        if (!Gateway::parseArgs(rawArgs, argsDoc) || !argsDoc.is<JsonObject>()) {
            *description = "args must be a JSON object or base64-encoded JSON";
            return OpenResult::BAD_ARGS;
        }
        JsonObjectConst args = argsDoc.as<JsonObjectConst>();
        const char* sid = args["sid"].is<const char*>() ? args["sid"].as<const char*>() : nullptr;
        const char* ip = args["ip"].is<const char*>() ? args["ip"].as<const char*>() : nullptr;
        int64_t port = 0, maxDuration = 0, idle = 0;
        if (!TunnelLogic::isValidSid(sid)) {
            *description = "sid must be 16 lowercase hex characters";
            return OpenResult::BAD_ARGS;
        }
        if (ip == nullptr || !_readArgsInt(args, "port", &port) || !_readArgsInt(args, "max_duration_s", &maxDuration) ||
            !_readArgsInt(args, "idle_s", &idle) || !GatewayLogic::isValidPort(port)) {
            *description = "ip (string) and port (1-65535) are required; max_duration_s and idle_s are integers";
            return OpenResult::BAD_ARGS;
        }
        uint8_t octets[4];
        switch (Gateway::validateTarget(ip, octets)) {
            case Gateway::TargetCheck::NOT_PRIVATE:
                *description = "Target must be a private IPv4 literal";
                return OpenResult::TARGET_NOT_PRIVATE;
            case Gateway::TargetCheck::NOT_ALLOWED:
                *description = "Target is this device or a network/broadcast address";
                return OpenResult::TARGET_NOT_ALLOWED;
            default: break;
        }
        uint16_t ports[TunnelLogic::PORTS_MAX];
        size_t portCount = getPorts(ports, TunnelLogic::PORTS_MAX);
        if (!TunnelLogic::isPortAllowed((uint16_t)port, ports, portCount)) {
            *description = "Port is not in the tunnel allowlist (system shadow tunnel_ports)";
            return OpenResult::PORT_NOT_ALLOWED;
        }
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < TUNNEL_OPEN_MIN_FREE_HEAP ||
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < TUNNEL_OPEN_MIN_MAX_BLOCK) {
            *description = "Not enough internal heap for a tunnel session";
            return OpenResult::NO_MEMORY;
        }
        if (!_allocateBuffers()) {
            *description = "Could not allocate the tunnel buffers";
            return OpenResult::NO_MEMORY;
        }

        snprintf(_executionId, sizeof(_executionId), "%s", executionId);
        snprintf(_downTopic, sizeof(_downTopic), "%s/%s/d", topicBase, sid);
        snprintf(_upTopic, sizeof(_upTopic), "%s/%s/u", topicBase, sid);
        memcpy(_target, octets, sizeof(_target));
        _maxDurationS = TunnelLogic::clampMaxDurationS(maxDuration);
        _idleS = TunnelLogic::clampIdleS(idle);
        if (acquireMutex(&_stateMutex)) {
            snprintf(_info.sid, sizeof(_info.sid), "%s", sid);
            snprintf(_info.ip, sizeof(_info.ip), "%s", ip);
            _info.port = (uint16_t)port;
            _info.since = CustomTime::getUnixTime();
            releaseMutex(&_stateMutex);
        }
        _resetSessionState(millis());
        _phase.store(Phase::ACTIVE);
        xTaskNotify(_taskHandle, NOTIFY_WORK_BIT, eSetBits);

        LOG_INFO("Tunnel %s opened: sid %s, target %s:%u, max %lu s, idle %lu s", executionId, sid, ip, (unsigned)port,
                 (unsigned long)_maxDurationS, (unsigned long)_idleS);
        Shadow::requestReport("system");
        return OpenResult::ACCEPTED;
    }

    bool closeFromCloud(JsonVariantConst rawArgs) {
        SpiRamAllocator allocator;
        JsonDocument argsDoc(&allocator);
        if (!Gateway::parseArgs(rawArgs, argsDoc)) return false;
        const char* sid = argsDoc["sid"].is<const char*>() ? argsDoc["sid"].as<const char*>() : nullptr;
        if (sid == nullptr || _phase.load() != Phase::ACTIVE || strcmp(sid, _info.sid) != 0) return false;
        requestClose(CloseReason::CLOUD_CLOSED);
        return true;
    }

    void requestClose(CloseReason reason) {
        if (_phase.load() != Phase::ACTIVE) return;
        uint8_t expected = 0;
        _closeRequest.compare_exchange_strong(expected, (uint8_t)reason);
        if (_taskHandle != nullptr) xTaskNotify(_taskHandle, NOTIFY_WORK_BIT, eSetBits);
    }

    void closeAndWait(CloseReason reason, uint32_t timeoutMs) {
        requestClose(reason);
        for (uint32_t waited = 0; _phase.load() == Phase::ACTIVE && waited < timeoutMs; waited += 10) delay(10);
    }

    const char* downlinkTopic() { return _phase.load() == Phase::IDLE ? nullptr : _downTopic; }
    const char* uplinkTopic() { return _phase.load() == Phase::IDLE ? nullptr : _upTopic; }

    bool pushDownlink(const char* topic, const uint8_t* payload, size_t length) {
        Phase phase = _phase.load();
        if (phase == Phase::IDLE || strcmp(topic, _downTopic) != 0) return false;
        if (phase == Phase::DONE || length > TunnelLogic::FRAME_MAX) {
            _rxDropped++;
            return true;
        }
        uint8_t index;
        if (xQueueReceive(_rxFree, &index, 0) != pdTRUE) {
            _rxDropped++; // the ARQ resends it
            return true;
        }
        memcpy(_rxPool[index].data, payload, length);
        _rxPool[index].length = (uint16_t)length;
        xQueueSend(_rxReady, &index, 0);
        xTaskNotify(_taskHandle, NOTIFY_WORK_BIT, eSetBits);
        return true;
    }

    bool takeUplink(const uint8_t** frame, size_t* length) {
        if (_phase.load() == Phase::IDLE || _uplinkTaken >= 0) return false;
        uint8_t index;
        if (xQueueReceive(_outReady, &index, 0) != pdTRUE) return false;
        _uplinkTaken = index;
        *frame = _outPool[index].data;
        *length = _outPool[index].length;
        return true;
    }

    void releaseUplink() {
        if (_uplinkTaken < 0) return;
        uint8_t index = (uint8_t)_uplinkTaken;
        _uplinkTaken = -1;
        xQueueSend(_outFree, &index, 0);
    }

    bool takeTerminal(char* executionId, size_t executionIdSize, char** responseJson) {
        if (_phase.load() != Phase::DONE || uxQueueMessagesWaiting(_outReady) > 0 || _uplinkTaken >= 0) return false;
        snprintf(executionId, executionIdSize, "%s", _executionId);
        *responseJson = _terminalJson;
        _terminalJson = nullptr;
        return true;
    }

    void finish() {
        if (_phase.load() != Phase::DONE) return;
        free(_terminalJson);
        _terminalJson = nullptr;
        _freeBuffers();
        _phase.store(Phase::IDLE);
        Shadow::requestReport("system");
    }

    void setCloudConnected(bool connected) { _cloudConnected.store(connected); }

    TaskInfo getTaskInfo() { return getTaskInfoSafely(_taskHandle, TUNNEL_TASK_STACK_SIZE, &_heartbeat); }
}
