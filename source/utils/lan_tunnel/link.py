# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Transport-agnostic session engine shared by the operator tool and the
reference device used in the host tests.

`Link` is the ARQ (sequencing, acks, retransmission, rate limit) over any
frame transport. `Bridge` moves one channel's bytes between an asyncio stream
pair and the link under credit flow control. Everything runs on one asyncio
loop; transports hand received frames over with `loop.call_soon_threadsafe`.
"""

from __future__ import annotations

import asyncio
import collections
import time
from typing import Callable

from . import protocol as p

ACK_DELAY_MS = 40
ACK_EVERY_FRAMES = 2
PUMP_INTERVAL_S = 0.01


def now_ms() -> int:
    return int(time.monotonic() * 1000)


class Link:
    def __init__(
        self,
        send_raw: Callable[[bytes], None],
        deliver: Callable[[p.Header, bytes], None],
        *,
        rate: int = 25,
        burst: int = 8,
        clock: Callable[[], int] = now_ms,
    ) -> None:
        self.send_raw = send_raw
        self.deliver = deliver
        self.clock = clock
        self.sender = p.ArqSender()
        self.receiver = p.ArqReceiver()
        self.bucket = p.TokenBucket(rate, burst, clock())
        self.queue: collections.deque[tuple[p.Type, int, bytes]] = collections.deque()
        self.connected = True
        self.last_rx_ms = clock()
        self._unacked = 0
        self._unacked_since = 0
        self._ack_now = False
        self._pong_due = False
        self.on_pong: Callable[[], None] | None = None
        self.frames_retx = 0
        self.frames_dropped = 0

    # Sending -----------------------------------------------------------

    def submit(self, t: p.Type, channel: int = 0, payload: bytes = b"") -> None:
        """Queue a sequenced frame; it goes out as the window and rate allow."""
        self.queue.append((t, channel, payload))

    def ping(self) -> None:
        self._emit_unsequenced(p.Type.PING)

    @property
    def backlog(self) -> int:
        return len(self.queue) + len(self.sender.slots)

    def _header(self, t: p.Type, channel: int, seq: int, flags: int = 0) -> p.Header:
        return p.Header(t, channel, flags | p.FLAG_ACK_VALID, seq, self.receiver.ack, self.receiver.sack)

    def _send(self, header: p.Header, payload: bytes) -> None:
        self._unacked = 0
        self._ack_now = False
        if self.connected:
            self.send_raw(p.encode(header, payload))

    def _emit_unsequenced(self, t: p.Type) -> bool:
        if not self.bucket.take(self.clock()):
            return False
        self._send(self._header(t, 0, 0), b"")
        return True

    def set_connected(self, connected: bool) -> None:
        if connected and not self.connected:
            self.sender.rearm_all()
            self.last_rx_ms = self.clock()
        self.connected = connected

    def pump(self) -> None:
        now = self.clock()
        if self.connected:
            for _ in range(p.WINDOW):
                due = self.sender.next_retransmit(now)
                if due is None or not self.bucket.take(now):
                    break
                slot, timeout = due
                t, channel, payload = slot.item
                self._send(self._header(t, channel, slot.seq, p.FLAG_RETX), payload)
                self.sender.mark_retransmitted(slot, now, timeout)
                self.frames_retx += 1
        while self.queue and self.sender.can_send() and self.bucket.take(now):
            item = self.queue.popleft()
            seq = self.sender.allocate(now, item)
            self._send(self._header(item[0], item[1], seq), item[2])
        if self._pong_due and self._emit_unsequenced(p.Type.PONG):
            self._pong_due = False
        ack_due = (
            self._ack_now
            or self._unacked >= ACK_EVERY_FRAMES
            or (self._unacked > 0 and now - self._unacked_since >= ACK_DELAY_MS)
        )
        if ack_due:
            self._emit_unsequenced(p.Type.ACK)

    # Receiving ---------------------------------------------------------

    def on_raw(self, frame: bytes) -> None:
        try:
            header, payload = p.decode(frame)
        except p.DecodeError:
            self.frames_dropped += 1
            return
        now = self.clock()
        self.last_rx_ms = now
        if header.flags & p.FLAG_ACK_VALID:
            self.sender.on_ack(header.ack, header.sack, now)
        if not p.is_sequenced(header.type):
            if header.type == p.Type.PING:
                self._pong_due = True
            elif header.type == p.Type.PONG and self.on_pong:
                self.on_pong()
            return
        verdict = self.receiver.on_frame(header.seq, (header, payload))
        if verdict == p.ArqReceiver.DELIVER:
            if self._unacked == 0:
                self._unacked_since = now
            self._unacked += 1
            self.deliver(header, payload)
            while (ready := self.receiver.pop_ready()) is not None:
                self._unacked += 1
                self.deliver(*ready[0])
        else:
            self._ack_now = True


class Bridge:
    """One channel: local asyncio stream <-> link, under credit both ways."""

    def __init__(
        self,
        link: Link,
        channel: int,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
        *,
        peer_credit: int,
        own_credit: int,
        on_closed: Callable[["Bridge"], None],
    ) -> None:
        self.link = link
        self.channel = channel
        self.reader = reader
        self.writer = writer
        self.out = p.SendCredit(grant=peer_credit)
        self.peer_initial = peer_credit
        self.inc = p.ReceiveCredit(own_credit)
        self.fin_sent = False
        self.remote_fin = False
        self.reset = False
        self.closed = False
        self.on_closed = on_closed
        self._credit_event = asyncio.Event()
        self._task: asyncio.Task | None = None

    def start(self) -> None:
        self._task = asyncio.get_running_loop().create_task(self._read_loop())

    async def _read_loop(self) -> None:
        try:
            while not self.reset and not self.closed:
                while self.out.available == 0 or self.link.backlog >= p.WINDOW:
                    self._credit_event.clear()
                    await self._credit_event.wait()
                    if self.reset or self.closed:
                        return
                data = await self.reader.read(min(self.out.available, p.DATA_MAX))
                if self.reset or self.closed:
                    return
                if not data:
                    self.link.submit(p.Type.FIN, self.channel)
                    self.fin_sent = True
                    self._maybe_done()
                    return
                self.out.sent += len(data)
                self.link.submit(p.Type.DATA, self.channel, data)
        except (ConnectionError, OSError):
            self.abort(notify=True)

    def wake(self) -> None:
        self._credit_event.set()

    # Peer frames
    def on_data(self, payload: bytes) -> None:
        if self.remote_fin or not self.inc.accepts(len(payload)):
            self.abort(notify=True, reason=p.RstReason.PROTOCOL)
            return
        self.inc.received += len(payload)
        try:
            self.writer.write(payload)
        except (ConnectionError, OSError, RuntimeError):
            self.abort(notify=True)

    def on_credit(self, cumulative: int) -> None:
        self.out.on_grant(cumulative)
        self.wake()
        self._maybe_done()

    def on_fin(self) -> None:
        self.remote_fin = True
        try:
            if self.writer.can_write_eof():
                self.writer.write_eof()
        except (ConnectionError, OSError, RuntimeError):
            pass
        self._maybe_done()

    def on_rst(self) -> None:
        self.abort(notify=False)

    # Housekeeping, called from the pump
    def service(self) -> None:
        if self.closed:
            return
        transport = self.writer.transport
        buffered = transport.get_write_buffer_size() if not transport.is_closing() else 0
        self.inc.drained = self.inc.received - buffered
        if self.inc.should_advertise():
            self.inc.advertised = self.inc.grant
            self.link.submit(p.Type.CH_CREDIT, self.channel, p.u32(self.inc.grant))
        if self.link.backlog < p.WINDOW:
            self.wake()
        self._maybe_done()

    def _maybe_done(self) -> None:
        # Both FINs, every byte we sent drained by the peer, our buffer flushed.
        drained = self.out.grant == (self.out.sent + self.peer_initial) & 0xFFFFFFFF
        if self.fin_sent and self.remote_fin and drained and self.inc.received == self.inc.drained:
            self._finish()

    def abort(self, notify: bool, reason: p.RstReason = p.RstReason.ABORT) -> None:
        if self.closed:
            return
        if notify:
            self.link.submit(p.Type.RST, self.channel, bytes([int(reason)]))
        self.reset = True
        try:
            self.writer.transport.abort()
        except Exception:
            pass
        self._finish()

    def _finish(self) -> None:
        if self.closed:
            return
        self.closed = True
        self.wake()
        if not self.reset:
            try:
                self.writer.close()
            except Exception:
                pass
        self.on_closed(self)
