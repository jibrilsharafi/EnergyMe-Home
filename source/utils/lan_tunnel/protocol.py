# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""LAN tunnel wire protocol, the Python twin of source/lib/tunnel_logic.

Frame codec, ARQ sender/receiver, RTO estimator, channel credit and token
bucket. Pinned to the firmware by source/test/test_tunnel_logic/golden_frames.h.
"""

from __future__ import annotations

import enum
import struct
from dataclasses import dataclass

VERSION = 1
HEADER_SIZE = 16
DATA_MAX = 4096
FRAME_MAX = HEADER_SIZE + DATA_MAX
CHANNELS_MAX = 4
WINDOW = 8
FLAG_ACK_VALID = 0x01
FLAG_RETX = 0x02

RTO_INITIAL_MS = 1000
RTO_MIN_MS = 300
RTO_MAX_MS = 5000
LINK_FAILED_TIMEOUTS = 8
FAST_RETRANSMIT_SACKS = 1  # see tunnel_logic.h: holes are resent every ~SRTT
FAST_RETRANSMIT_MIN_MS = 20
HOLE_TRANSMISSIONS_MAX = 6  # then the counted RTO path takes over (LINK_FAILED)
CREDIT_UPDATE_BYTES = 2048

_HEADER = struct.Struct("<BBBBIIHH")
_OPENED = struct.Struct("<HBBIII")


class Type(enum.IntEnum):
    DATA = 1
    ACK = 2
    CH_OPEN = 3
    CH_OK = 4
    CH_ERR = 5
    CH_CREDIT = 6
    FIN = 7
    RST = 8
    OPENED = 9
    CLOSE = 10
    CLOSED = 11
    PING = 12
    PONG = 13


class ChannelError(enum.IntEnum):
    CONNECT_FAILED = 1
    TIMEOUT = 2
    REFUSED = 3
    NO_SOCKET = 4
    BUSY = 5


class RstReason(enum.IntEnum):
    ABORT = 0
    LAN_RESET = 1
    WRITE_STALL = 2
    NOT_OPEN = 3
    PROTOCOL = 4
    SESSION_CLOSED = 5


class CloseReason(enum.IntEnum):
    NONE = 0
    OPERATOR_CLOSED = 1
    CLOUD_CLOSED = 2
    IDLE_TIMEOUT = 3
    MAX_DURATION = 4
    OPERATOR_LOST = 5
    TUNNEL_DISABLED = 6
    OTA_STARTED = 7
    LOW_MEMORY = 8
    CLOUD_DISCONNECTED = 9
    LINK_FAILED = 10
    BYTE_CAP = 11
    SHUTDOWN = 12


_CHANNEL_TYPES = {Type.DATA, Type.CH_OPEN, Type.CH_OK, Type.CH_ERR, Type.CH_CREDIT, Type.FIN, Type.RST}
_PAYLOAD_SIZE = {
    Type.CH_OPEN: 4,
    Type.CH_CREDIT: 4,
    Type.CH_ERR: 1,
    Type.RST: 1,
    Type.CLOSE: 1,
    Type.CLOSED: 1,
    Type.OPENED: _OPENED.size,
}


def is_sequenced(t: Type) -> bool:
    return t not in (Type.ACK, Type.PING, Type.PONG)


def seq_before(a: int, b: int) -> bool:
    """32-bit serial arithmetic: a comes before b."""
    return ((a - b) & 0xFFFFFFFF) >= 0x80000000


class DecodeError(ValueError):
    pass


@dataclass
class Header:
    type: Type
    channel: int = 0
    flags: int = FLAG_ACK_VALID
    seq: int = 0
    ack: int = 0
    sack: int = 0
    length: int = 0


def encode(header: Header, payload: bytes = b"") -> bytes:
    if len(payload) > DATA_MAX:
        raise ValueError("payload above DATA_MAX")
    return (
        _HEADER.pack(
            VERSION, int(header.type), header.channel, header.flags, header.seq, header.ack, header.sack, len(payload)
        )
        + payload
    )


def decode(frame: bytes) -> tuple[Header, bytes]:
    """Validates a whole frame exactly like TunnelLogic::decodeFrame."""
    if len(frame) < HEADER_SIZE:
        raise DecodeError("SHORT")
    ver, t, ch, flags, seq, ack, sack, length = _HEADER.unpack_from(frame)
    if ver != VERSION:
        raise DecodeError("BAD_VERSION")
    try:
        t = Type(t)
    except ValueError:
        raise DecodeError("BAD_TYPE") from None
    if (ch == 0 or ch > CHANNELS_MAX) if t in _CHANNEL_TYPES else ch != 0:
        raise DecodeError("BAD_CHANNEL")
    if length > DATA_MAX:
        raise DecodeError("TOO_LONG")
    if HEADER_SIZE + length != len(frame):
        raise DecodeError("LENGTH_MISMATCH")
    expected = _PAYLOAD_SIZE.get(t, 0)
    if (length == 0) if t == Type.DATA else (length != expected):
        raise DecodeError("BAD_PAYLOAD")
    if (seq == 0) if is_sequenced(t) else (seq != 0):
        raise DecodeError("BAD_PAYLOAD")
    return Header(t, ch, flags, seq, ack, sack, length), bytes(frame[HEADER_SIZE:])


@dataclass
class Opened:
    frame_max: int
    window: int
    channels_max: int
    channel_credit: int
    max_duration_s: int
    idle_s: int

    def encode(self) -> bytes:
        return _OPENED.pack(
            self.frame_max, self.window, self.channels_max, self.channel_credit, self.max_duration_s, self.idle_s
        )

    @classmethod
    def decode(cls, payload: bytes) -> "Opened":
        return cls(*_OPENED.unpack(payload))


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def get_u32(payload: bytes) -> int:
    return struct.unpack_from("<I", payload)[0]


# ---------------------------------------------------------------------------
# ARQ
# ---------------------------------------------------------------------------


class ArqReceiver:
    DELIVER, BUFFER, DUPLICATE, OUT_OF_WINDOW = range(4)

    def __init__(self, ack: int = 0) -> None:
        self.ack = ack & 0xFFFFFFFF
        self._have: dict[int, object] = {}  # seq -> buffered item

    def on_frame(self, seq: int, item: object = None) -> int:
        offset = (seq - self.ack) & 0xFFFFFFFF
        if offset == 0 or offset >= 0x80000000:
            return self.DUPLICATE
        if offset > WINDOW:
            return self.OUT_OF_WINDOW
        if offset == 1:
            self.ack = seq
            return self.DELIVER
        if seq in self._have:
            return self.DUPLICATE
        self._have[seq] = item
        return self.BUFFER

    def pop_ready(self):
        """Next buffered item now in order, or None."""
        nxt = (self.ack + 1) & 0xFFFFFFFF
        if nxt not in self._have:
            return None
        self.ack = nxt
        return (self._have.pop(nxt),)

    @property
    def sack(self) -> int:
        bits = 0
        for i in range(WINDOW - 1):
            if ((self.ack + 2 + i) & 0xFFFFFFFF) in self._have:
                bits |= 1 << i
        return bits


class RtoEstimator:
    def __init__(self) -> None:
        self.srtt = 0
        self.rttvar = 0
        self.rto = RTO_INITIAL_MS
        self.has_sample = False

    @staticmethod
    def _clamp(value: int) -> int:
        return max(RTO_MIN_MS, min(RTO_MAX_MS, value))

    def sample(self, rtt_ms: int) -> None:
        if not self.has_sample:
            self.srtt = rtt_ms
            self.rttvar = rtt_ms // 2
            self.has_sample = True
        else:
            self.rttvar = (3 * self.rttvar + abs(self.srtt - rtt_ms)) // 4
            self.srtt = (7 * self.srtt + rtt_ms) // 8
        self.rto = self._clamp(self.srtt + 4 * self.rttvar)

    def backoff(self) -> None:
        self.rto = self._clamp(self.rto * 2)

    def clear_backoff(self, observed_ms: int) -> None:
        """Ack progress without a Karn sample: drop the backoff, never below observed_ms."""
        base = self._clamp(self.srtt + 4 * self.rttvar if self.has_sample else RTO_INITIAL_MS)
        self.rto = self._clamp(max(base, min(observed_ms, 2 * base)))

    def hole_wait(self) -> int:
        if not self.has_sample:
            return self.rto
        return max(self.srtt + self.srtt // 4, FAST_RETRANSMIT_MIN_MS)


@dataclass
class _Slot:
    seq: int
    sent_ms: int
    item: object
    transmissions: int = 1
    sacked: bool = False
    hole: bool = False  # a later frame was SACKed
    rearmed: bool = False  # resend after a reconnect: not an RTO event
    first_sent_ms: int = -1

    def __post_init__(self) -> None:
        if self.first_sent_ms < 0:
            self.first_sent_ms = self.sent_ms


class ArqSender:
    def __init__(self, next_seq: int = 1) -> None:
        self.next_seq = next_seq & 0xFFFFFFFF
        self.slots: dict[int, _Slot] = {}  # seq -> slot
        self.timeouts = 0
        self.rto = RtoEstimator()

    def can_send(self) -> bool:
        return len(self.slots) < WINDOW

    def allocate(self, now_ms: int, item: object) -> int:
        seq = self.next_seq
        self.next_seq = (self.next_seq + 1) & 0xFFFFFFFF
        self.slots[seq] = _Slot(seq, now_ms, item)
        return seq

    def on_ack(self, ack: int, sack: int, now_ms: int) -> int:
        if seq_before((self.next_seq - 1) & 0xFFFFFFFF, ack):
            return 0
        freed = [s for s in self.slots.values() if not seq_before(ack, s.seq)]
        sample = None
        for s in freed:
            if s.transmissions == 1 and (sample is None or seq_before(sample.seq, s.seq)):
                sample = s
            del self.slots[s.seq]
        if sample is not None:
            self.rto.sample(now_ms - sample.sent_ms)
        elif freed:
            newest = freed[0]
            for s in freed[1:]:
                if seq_before(newest.seq, s.seq):
                    newest = s
            self.rto.clear_backoff(now_ms - newest.first_sent_ms)
        if freed:
            self.timeouts = 0
        for i in range(16):
            if sack & (1 << i):
                s = self.slots.get((ack + 2 + i) & 0xFFFFFFFF)
                if s:
                    s.sacked = True
        for s in self.slots.values():
            if s.sacked:
                continue
            later = sum(1 for o in self.slots.values() if o.sacked and seq_before(s.seq, o.seq))
            if later >= FAST_RETRANSMIT_SACKS:
                s.hole = True
        return len(freed)

    def next_retransmit(self, now_ms: int):
        """(slot, timeout) due for retransmission, or None."""
        hole_wait = self.rto.hole_wait()
        uncounted = [
            s for s in self.slots.values()
            if not s.sacked
            and (s.rearmed or (s.hole and s.transmissions < HOLE_TRANSMISSIONS_MAX and now_ms - s.sent_ms >= hole_wait))
        ]
        if uncounted:
            oldest = uncounted[0]
            for s in uncounted[1:]:
                if seq_before(s.seq, oldest.seq):
                    oldest = s
            return oldest, False
        due = [s for s in self.slots.values() if not s.sacked and now_ms - s.sent_ms >= self.rto.rto]
        if not due:
            return None
        oldest = due[0]
        for s in due[1:]:
            if seq_before(s.seq, oldest.seq):
                oldest = s
        return oldest, True

    def mark_retransmitted(self, slot: _Slot, now_ms: int, timeout: bool) -> None:
        slot.rearmed = False
        slot.transmissions += 1
        slot.sent_ms = now_ms
        if not timeout:
            return
        if any(seq_before(o.seq, slot.seq) for o in self.slots.values()):
            return
        # Every other frame already expired belongs to this same event: resend now.
        for o in self.slots.values():
            if o is not slot and not o.sacked and now_ms - o.sent_ms >= self.rto.rto:
                o.rearmed = True
        self.rto.backoff()
        self.timeouts += 1

    def rearm_all(self) -> None:
        for s in self.slots.values():
            if not s.sacked:
                s.rearmed = True


@dataclass
class SendCredit:
    grant: int = 0
    sent: int = 0

    @property
    def available(self) -> int:
        return (self.grant - self.sent) & 0xFFFFFFFF if seq_before(self.sent, self.grant) else 0

    def on_grant(self, cumulative: int) -> None:
        if seq_before(self.grant, cumulative):
            self.grant = cumulative


@dataclass
class ReceiveCredit:
    buffer_size: int
    received: int = 0
    drained: int = 0
    advertised: int = 0

    def __post_init__(self) -> None:
        self.advertised = self.buffer_size

    def accepts(self, n: int) -> bool:
        return n <= self.advertised - self.received

    @property
    def grant(self) -> int:
        return self.drained + self.buffer_size

    def should_advertise(self) -> bool:
        fresh = self.grant - self.advertised
        return fresh >= CREDIT_UPDATE_BYTES or (fresh > 0 and self.received == self.drained)


class TokenBucket:
    def __init__(self, rate_per_s: int, burst: int, now_ms: int) -> None:
        self.rate = rate_per_s
        self.burst_milli = burst * 1000
        self.milli = self.burst_milli
        self.last = now_ms

    def take(self, now_ms: int) -> bool:
        elapsed = max(0, now_ms - self.last)
        self.last = now_ms
        self.milli = min(self.burst_milli, self.milli + elapsed * self.rate)
        if self.milli < 1000:
            return False
        self.milli -= 1000
        return True
