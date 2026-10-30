# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Operator side of a tunnel session: a local TCP listener whose connections
become tunnel channels (one channel per accepted connection, at most
`channels_max` at once; extra accepts wait for a free channel id)."""

from __future__ import annotations

import asyncio
import logging
from typing import Callable

from . import protocol as p
from .link import PUMP_INTERVAL_S, Bridge, Link, now_ms

log = logging.getLogger("lan_tunnel")

OPERATOR_CREDIT = 64 * 1024
PING_INTERVAL_MS = 5_000  # six chances inside the device's 30 s silence limit
DEVICE_SILENCE_MS = 30_000
OPEN_TIMEOUT_S = 15
CHANNEL_OPEN_TIMEOUT_S = 30  # several backed-off RTOs on a lossy link
CLOSE_WAIT_S = 3


class SessionEnded(Exception):
    pass


class OperatorSession:
    def __init__(self, send_raw: Callable[[bytes], None], *, rate: int = 25, credit: int = OPERATOR_CREDIT) -> None:
        self.link = Link(send_raw, self._deliver, rate=rate)
        self.credit = credit
        self.opened: p.Opened | None = None
        self.closed_reason: p.CloseReason | None = None
        self._opened_event = asyncio.Event()
        self._closed_event = asyncio.Event()
        self._channels: dict[int, Bridge | None] = {}  # id -> bridge, None while opening
        self._pending: dict[int, tuple] = {}  # id -> (reader, writer, reply future) until CH_OK/CH_ERR
        self._skip_replies: dict[int, int] = {}  # replies owed to CH_OPENs we gave up on
        self._dropping: set[int] = set()
        self._free = asyncio.Condition()
        self._last_ping = now_ms()
        self._pump_task: asyncio.Task | None = None
        self.channels_opened = 0

    # Transport entry points -------------------------------------------

    def on_raw(self, frame: bytes) -> None:
        self.link.on_raw(frame)

    def set_connected(self, connected: bool) -> None:
        self.link.set_connected(connected)

    # Lifecycle ----------------------------------------------------------

    def start(self) -> None:
        self._pump_task = asyncio.get_running_loop().create_task(self._pump())

    async def wait_opened(self, timeout: float = OPEN_TIMEOUT_S) -> p.Opened:
        await asyncio.wait_for(self._opened_event.wait(), timeout)
        assert self.opened is not None
        return self.opened

    async def wait_closed(self) -> p.CloseReason:
        await self._closed_event.wait()
        return self.closed_reason or p.CloseReason.NONE

    async def close(self, reason: p.CloseReason = p.CloseReason.OPERATOR_CLOSED) -> None:
        """CLOSE handshake: wait briefly for CLOSED, then stop regardless."""
        if not self._closed_event.is_set():
            self.link.submit(p.Type.CLOSE, 0, bytes([int(reason)]))
            try:
                await asyncio.wait_for(self._closed_event.wait(), CLOSE_WAIT_S)
            except asyncio.TimeoutError:
                pass
        self._end(self.closed_reason or reason)

    def _end(self, reason: p.CloseReason) -> None:
        if self.closed_reason is None:
            self.closed_reason = reason
        for bridge in list(self._channels.values()):
            if bridge is not None:
                bridge.abort(notify=False)
        for _, _, fut in self._pending.values():
            if not fut.done():
                fut.set_exception(SessionEnded())
        self._closed_event.set()
        asyncio.get_running_loop().create_task(self._release())  # wake connections waiting for an id
        if self._pump_task and self._pump_task is not asyncio.current_task():
            self._pump_task.cancel()

    async def _pump(self) -> None:
        while not self._closed_event.is_set():
            now = now_ms()
            if now - self._last_ping >= PING_INTERVAL_MS and self.link.ping():
                self._last_ping = now
            if self.link.connected and now - self.link.last_rx_ms >= DEVICE_SILENCE_MS:
                log.warning("device silent for %d s, giving up", DEVICE_SILENCE_MS // 1000)
                self._end(p.CloseReason.OPERATOR_LOST)
                return
            for bridge in list(self._channels.values()):
                if bridge is not None:
                    bridge.service()
            self.link.pump()
            await asyncio.sleep(PUMP_INTERVAL_S)

    # Channels -----------------------------------------------------------

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        """asyncio.start_server callback: one local connection = one channel."""
        channel = None
        try:
            opened = await self.wait_opened()
            async with self._free:
                await self._free.wait_for(lambda: self._closed_event.is_set() or self._free_id(opened) is not None)
                if self._closed_event.is_set():
                    raise SessionEnded()
                channel = self._free_id(opened)
                self._channels[channel] = None
            for _ in range(2):  # BUSY: the device has just reset its stale channel, one retry
                fut = asyncio.get_running_loop().create_future()
                self._pending[channel] = (reader, writer, fut)
                self.link.submit(p.Type.CH_OPEN, channel, p.u32(self.credit))
                error = await asyncio.wait_for(fut, CHANNEL_OPEN_TIMEOUT_S)
                if error != p.ChannelError.BUSY:
                    break
        except (SessionEnded, asyncio.TimeoutError, ConnectionError):
            if channel is not None and self._pending.pop(channel, None) is not None:
                # The device still owes a reply to this CH_OPEN: skip it (and, after a
                # late CH_OK, the old channel's frames) so it cannot answer a reuse.
                self._skip_replies[channel] = self._skip_replies.get(channel, 0) + 1
                self.link.submit(p.Type.RST, channel, bytes([int(p.RstReason.ABORT)]))
            if channel is not None and self._channels.get(channel) is None:
                await self._release(channel)
            writer.transport.abort()
            return
        if error is not None:
            log.warning("channel %d: device could not connect (%s)", channel, p.ChannelError(error).name)
            await self._release(channel)
            writer.transport.abort()

    def _free_id(self, opened: p.Opened):
        for channel in range(1, min(opened.channels_max, p.CHANNELS_MAX) + 1):
            if channel not in self._channels:
                return channel
        return None

    def _on_bridge_closed(self, bridge: Bridge) -> None:
        asyncio.get_running_loop().create_task(self._release(bridge.channel))

    async def _release(self, channel: int | None = None) -> None:
        async with self._free:
            if channel is not None:
                self._channels.pop(channel, None)
            self._free.notify_all()

    # Frames from the device -------------------------------------------

    def _channel_reply(self, h: p.Header, payload: bytes) -> None:
        if self._skip_replies.get(h.channel, 0) > 0:
            self._skip_replies[h.channel] -= 1
            if h.type == p.Type.CH_OK:
                self._dropping.add(h.channel)  # the old channel's frames follow until our RST lands
            return
        self._dropping.discard(h.channel)
        pending = self._pending.pop(h.channel, None)
        if pending is None:
            return
        reader, writer, fut = pending
        if h.type == p.Type.CH_OK:
            # Created now, not when handle_client resumes: a server that speaks first
            # (SSH, telnet banners) has its DATA right behind this CH_OK.
            assert self.opened is not None
            bridge = Bridge(
                self.link, h.channel, reader, writer,
                peer_credit=self.opened.channel_credit, own_credit=self.credit, on_closed=self._on_bridge_closed,
            )
            self._channels[h.channel] = bridge
            self.channels_opened += 1
            bridge.start()
            error = None
        else:
            error = payload[0]
        if not fut.done():
            fut.set_result(error)

    def _deliver(self, h: p.Header, payload: bytes) -> None:
        if h.type == p.Type.OPENED:
            self.opened = p.Opened.decode(payload)
            self._opened_event.set()
        elif h.type == p.Type.CLOSED:
            reason = p.CloseReason(payload[0]) if payload[0] in p.CloseReason._value2member_map_ else p.CloseReason.NONE
            self._end(reason)
        elif h.type in (p.Type.CH_OK, p.Type.CH_ERR):
            self._channel_reply(h, payload)
        elif h.channel in self._dropping:
            return
        else:
            bridge = self._channels.get(h.channel)
            if bridge is None:
                return
            if h.type == p.Type.DATA:
                bridge.on_data(payload)
            elif h.type == p.Type.CH_CREDIT:
                bridge.on_credit(p.get_u32(payload))
            elif h.type == p.Type.FIN:
                bridge.on_fin()
            elif h.type == p.Type.RST:
                bridge.on_rst()
